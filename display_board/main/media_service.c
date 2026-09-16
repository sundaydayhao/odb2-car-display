#include "media_service.h"
#include "audio_device.h"
#include "device_services.h"
#include "cloud_service.h"
#include "media_library.h"
#include "voice_codec_memory.h"
#include "recorder.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mp3_dec.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"
#include "freertos/idf_additions.h"

enum { STREAM_CAPACITY = 192 * 1024, INPUT_CAPACITY = 4096, PCM_CAPACITY = 4608 };
typedef struct { unsigned generation; uint32_t storage_generation; char path[AV_FILE_PATH_SIZE]; } fetch_job_t;
static const char *TAG = "media";
static QueueHandle_t s_commands, s_jobs;
static StreamBufferHandle_t s_stream;
static StaticStreamBuffer_t s_stream_control;
static uint8_t *s_stream_storage;
static atomic_uint s_generation, s_received;
static atomic_bool s_fetch_busy, s_fetch_done;
static atomic_int s_fetch_error;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static media_status_t s_status = {.volume = 35};
/* Only the playback task owns the following session fields and decoder. */
static unsigned s_volume = 35, s_frames, s_bad_frames;
static bool s_selected;
static unsigned s_local_index;
static uint32_t s_storage_generation;
static char s_local_path[AV_FILE_PATH_SIZE], s_local_title[MEDIA_TITLE_SIZE];
static bool s_play, s_session, s_audio_owned, s_buffering;
static void *s_decoder;
static uint8_t *s_input, *s_pcm;
static size_t s_used, s_skip;
static unsigned s_junk;
static uint64_t s_played_us;
static int64_t s_volume_save_at;

static void publish(media_state_t state, const char *detail)
{
    portENTER_CRITICAL(&s_lock);
    s_status.state = state;
    s_status.selected=s_selected; s_status.local_index=s_local_index;
    s_status.storage_generation=s_storage_generation;
    s_status.volume = s_volume;
    s_status.played_ms = (uint32_t)(s_played_us / 1000);
    s_status.decoded_frames = s_frames;
    s_status.received_bytes = atomic_load(&s_received);
    s_status.buffered_bytes = s_stream ? xStreamBufferBytesAvailable(s_stream) : 0;
    snprintf(s_status.title, sizeof(s_status.title), "%s", s_local_title);
    snprintf(s_status.detail, sizeof(s_status.detail), "%s", detail);
    ++s_status.revision;
    portEXIT_CRITICAL(&s_lock);
}

void media_service_status(media_status_t *out)
{
    if (!out) return;
    portENTER_CRITICAL(&s_lock);
    *out = s_status;
    portEXIT_CRITICAL(&s_lock);
    out->network = device_network_ready();
}
bool media_service_request(const media_request_t *request)
{
    if (!request || !s_commands || request->action >= MEDIA_ACTION_COUNT) return false;
    if (request->action == MEDIA_SELECT_LOCAL && request->value >= AV_FILE_LIMIT) return false;
    if (request->action == MEDIA_VOLUME && request->value > 100) return false;
    return xQueueSend(s_commands, request, 0) == pdTRUE;
}

static bool cancelled(unsigned generation, uint32_t storage_generation)
{
    return atomic_load(&s_generation) != generation || cloud_ota_busy() ||
           !recorder_storage_ready() || recorder_storage_generation()!=storage_generation;
}
static void fetch_task(void *arg)
{
    (void)arg;
    uint8_t *block = heap_caps_malloc(2048, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    fetch_job_t job;
    while (xQueueReceive(s_jobs, &job, portMAX_DELAY) == pdTRUE) {
        int error = 0;
        uint32_t offset=0;
        if (!block) error=1;
        while (!error && !cancelled(job.generation,job.storage_generation)) {
            int n=media_library_read(job.path,job.storage_generation,offset,block,2048);
            if (n<0) { error=4; break; }
            if (!n) break;
            offset+=(unsigned)n; atomic_fetch_add(&s_received,(unsigned)n);
            size_t sent=0;
            while (sent<(size_t)n && !cancelled(job.generation,job.storage_generation))
                sent+=xStreamBufferSend(s_stream,block+sent,n-sent,pdMS_TO_TICKS(50));
        }
        if (!recorder_storage_ready() || recorder_storage_generation()!=job.storage_generation) error=4;
        atomic_store(&s_fetch_error, error);
        atomic_store(&s_fetch_done, true);
        atomic_store(&s_fetch_busy, false);
    }
}

static bool close_session(void)
{
    atomic_fetch_add(&s_generation, 1);
    if (s_audio_owned) audio_device_release(AUDIO_OWNER_MEDIA);
    s_audio_owned = false;
    if (s_decoder) esp_mp3_dec_close(s_decoder);
    s_decoder = NULL;
    s_session = false; s_used = s_skip = s_junk = 0;
    /* Wait for the SD reader cancellation acknowledgement
     * before resetting the stream; never reset under an active producer. */
    for (int i = 0; i < 250 && atomic_load(&s_fetch_busy); ++i) vTaskDelay(pdMS_TO_TICKS(20));
    if (atomic_load(&s_fetch_busy)) return false;
    return xStreamBufferReset(s_stream) == pdPASS;
}
static void save_volume(void)
{
    nvs_handle_t handle;
    if (nvs_open("media", NVS_READWRITE, &handle) == ESP_OK) {
        esp_err_t err = nvs_set_u8(handle, "volume", s_volume);
        if (err == ESP_OK) err = nvs_commit(handle);
        if (err != ESP_OK) ESP_LOGW(TAG, "Volume save: %s", esp_err_to_name(err));
        nvs_close(handle);
    }
    s_volume_save_at = 0;
}
static void command(const media_request_t *request)
{
    switch (request->action) {
    case MEDIA_SELECT_LOCAL: {
        char path[AV_FILE_PATH_SIZE], title[MEDIA_TITLE_SIZE]; uint32_t generation;
        if (!media_library_music(request->value,request->revision,path,sizeof(path),title,sizeof(title),&generation)) {
            publish(s_status.state,"文件列表已变化，请刷新后选择"); break;
        }
        close_session(); s_selected=true; s_local_index=request->value; s_storage_generation=generation;
        strcpy(s_local_path,path); strcpy(s_local_title,title); s_play=true;
        s_played_us=0; s_frames=s_bad_frames=0;
        publish(MEDIA_CONNECTING,"正在读取音乐");
        break;
    }
    case MEDIA_PREVIOUS:
    case MEDIA_NEXT: {
        media_request_t next={0};
        av_library_status_t library; media_library_status(&library);
        if (!library.storage || !library.music_count) { s_play=false; close_session(); publish(MEDIA_ERROR,"SD 卡没有可播放的音乐"); break; }
        next.action=MEDIA_SELECT_LOCAL; next.revision=library.revision;
        next.value=s_selected?(s_local_index+library.music_count+(request->action==MEDIA_NEXT?1:-1))%library.music_count:0;
        command(&next); break;
    }
    case MEDIA_TOGGLE:
        if (!s_selected) {
            av_library_status_t library; media_library_status(&library);
            if (!library.storage || !library.music_count) { publish(MEDIA_ERROR,"SD 卡没有可播放的音乐"); break; }
            media_request_t first={.action=MEDIA_SELECT_LOCAL,.revision=library.revision};
            command(&first); break;
        }
        s_play = !s_play;
        if (s_play && !s_session) { s_played_us = 0; s_frames = s_bad_frames = 0; }
        if (!s_play) {
            if (s_audio_owned) { audio_device_release(AUDIO_OWNER_MEDIA); s_audio_owned = false; }
            publish(MEDIA_PAUSED, "已暂停");
        } else publish(MEDIA_BUFFERING, "正在准备播放");
        break;
    case MEDIA_STOP:
        s_play = false; close_session(); s_played_us = 0;
        publish(MEDIA_IDLE, "播放已停止");
        break;
    case MEDIA_VOLUME:
        s_volume = request->value;
        if (s_audio_owned) audio_device_volume(AUDIO_OWNER_MEDIA, s_volume);
        s_volume_save_at = esp_timer_get_time() + 1000000;
        portENTER_CRITICAL(&s_lock);
        s_status.volume = s_volume; ++s_status.revision;
        portEXIT_CRITICAL(&s_lock);
        break;
    default: break;
    }
}

static bool open_session(void)
{
    if (atomic_load(&s_fetch_busy)) return false;
    if (!s_selected || !*s_local_path) { s_play=false; publish(MEDIA_ERROR,"请先选择音乐"); return false; }
    if (!media_codec_memory_begin()) {
        s_play=false; publish(MEDIA_ERROR,"音乐解码资源繁忙，请重试"); return false;
    }
    esp_audio_err_t decoder_error=esp_mp3_dec_open(NULL,0,&s_decoder);
    media_codec_memory_end();
    if (decoder_error != ESP_AUDIO_ERR_OK) {
        s_play = false; publish(MEDIA_ERROR, "音乐解码资源不足"); return false;
    }
    fetch_job_t job = {.generation = atomic_fetch_add(&s_generation, 1) + 1,
                      .storage_generation=s_storage_generation};
    snprintf(job.path, sizeof(job.path), "%s", s_local_path);
    xStreamBufferReset(s_stream);
    atomic_store(&s_fetch_done, false); atomic_store(&s_fetch_error, 0); atomic_store(&s_received, 0);
    atomic_store(&s_fetch_busy, true);
    if (xQueueSend(s_jobs, &job, 0) != pdTRUE) {
        atomic_store(&s_fetch_busy, false); close_session();
        s_play = false; publish(MEDIA_ERROR, "播放任务繁忙，请重试"); return false;
    }
    s_session = true; s_buffering = true; s_used = s_skip = s_junk = 0; s_bad_frames = 0;
    publish(MEDIA_CONNECTING, "正在读取音乐");
    return true;
}
static void failed(const char *detail)
{
    close_session(); s_play=false;
    publish(MEDIA_ERROR, detail);
}
static void playback_step(void)
{
    size_t available = xStreamBufferBytesAvailable(s_stream);
    bool done = atomic_load(&s_fetch_done);
    if (s_buffering && available + s_used < 8192 && !done) {
        publish(MEDIA_BUFFERING, "正在缓冲音频"); vTaskDelay(pdMS_TO_TICKS(20)); return;
    }
    s_buffering = false;
    if (s_used < INPUT_CAPACITY && available)
        s_used += xStreamBufferReceive(s_stream, s_input + s_used, INPUT_CAPACITY - s_used, 0);
    if (s_skip) {
        if (!s_used && done && !available) { failed("音乐标签数据不完整"); return; }
        if (!s_used) {
            s_buffering = true;
            vTaskDelay(pdMS_TO_TICKS(5));
            return;
        }
        size_t skip = s_skip < s_used ? s_skip : s_used;
        memmove(s_input, s_input + skip, s_used - skip); s_used -= skip; s_skip -= skip;
        return;
    }
    if (s_used >= 3 && !memcmp(s_input, "ID3", 3)) {
        int tag = media_id3_size(s_input, s_used);
        if (tag < 0) { failed("音乐标签格式不受支持"); return; }
        if (!tag && done && !xStreamBufferBytesAvailable(s_stream)) { failed("音乐标签数据不完整"); return; }
        if (!tag) { s_buffering = true; vTaskDelay(pdMS_TO_TICKS(5)); }
        if (tag > 0) s_skip = tag;
        return;
    }
    int frame_size = media_mp3_frame_size(s_input, s_used);
    if (frame_size > 0 && (size_t)frame_size <= s_used) {
        esp_audio_dec_in_raw_t raw = {.buffer = s_input, .len = frame_size};
        esp_audio_dec_out_frame_t frame = {.buffer = s_pcm, .len = PCM_CAPACITY};
        esp_audio_dec_info_t info = {0};
        /* The codec allocates its main workspace lazily on the first frame. */
        if (!media_codec_memory_begin()) { failed("音乐解码资源繁忙，请重试"); return; }
        esp_audio_err_t err = esp_mp3_dec_decode(s_decoder, &raw, &frame, &info);
        media_codec_memory_end();
        memmove(s_input, s_input + frame_size, s_used - frame_size); s_used -= frame_size;
        if (err != ESP_AUDIO_ERR_OK || raw.consumed != (unsigned)frame_size || !frame.decoded_size) {
            if (++s_bad_frames > 12) failed("音频无法解码，请更换文件");
            return;
        }
        s_bad_frames = 0; s_junk = 0;
        if (info.bits_per_sample != 16 || (info.channel != 1 && info.channel != 2) ||
            info.sample_rate < 8000 || info.sample_rate > 48000 || frame.decoded_size > PCM_CAPACITY ||
            frame.decoded_size % (2 * info.channel)) { failed("音频采样格式不受支持"); return; }
        size_t samples = frame.decoded_size / 2 / info.channel;
        int16_t *pcm = (int16_t *)s_pcm;
        if (info.channel == 2)
            for (size_t i = 0; i < samples; ++i) pcm[i] = ((int32_t)pcm[2*i] + pcm[2*i+1]) / 2;
        if (!s_audio_owned) s_audio_owned = audio_device_acquire(AUDIO_OWNER_MEDIA);
        if (!s_audio_owned) { failed("音频正在被语音使用，请稍后播放"); return; }
        /* Fractional-rate conversion has separate, lazily allocated workspaces. */
        if (!media_codec_memory_begin()) { failed("音乐解码资源繁忙，请重试"); return; }
        bool output_ok=audio_device_configure(AUDIO_OWNER_MEDIA,false,info.sample_rate,s_volume) &&
                       audio_device_write(AUDIO_OWNER_MEDIA,pcm,samples*2);
        media_codec_memory_end();
        if (!output_ok) { failed("扬声器不可用，请检查音频连接"); return; }
        ++s_frames; s_played_us += samples * 1000000ULL / info.sample_rate;
        if (s_frames == 1 || !(s_frames % 8)) publish(MEDIA_PLAYING, "正在播放音乐");
        /* CPU work may consume the whole audio interval with PSRAM state.
         * Give the idle/watchdog task a tick even when I2S never has to wait. */
        if (!(s_frames % 4)) vTaskDelay(1);
        return;
    }
    if (frame_size < 0 && s_used) {
        memmove(s_input, s_input + 1, --s_used);
        if (++s_junk > 65536) failed("文件不是可播放的 MP3 音频");
        return;
    }
    if (done && !xStreamBufferBytesAvailable(s_stream)) {
        int error = atomic_load(&s_fetch_error);
        if (error) failed("音乐读取失败，请重新选择");
        else {
            s_play = false; close_session();
            publish(s_frames ? MEDIA_FINISHED : MEDIA_ERROR, s_frames ? "播放结束" : "没有可播放的 MP3 音频");
        }
    } else {
        /* A partial MP3 frame also means an underrun. Keep it while refilling,
         * and report buffering instead of leaving a stale playing state. */
        s_buffering = true;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
static void playback_task(void *arg)
{
    (void)arg;
    s_input = heap_caps_malloc(INPUT_CAPACITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_pcm = heap_caps_malloc(PCM_CAPACITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_input || !s_pcm) {
        publish(MEDIA_ERROR, "多媒体内存不足");
        vTaskDeleteWithCaps(NULL); return;
    }
    media_request_t request;
    for (;;) {
        while (xQueueReceive(s_commands, &request, 0) == pdTRUE) command(&request);
        if (s_volume_save_at && esp_timer_get_time() >= s_volume_save_at) save_volume();
        if ((s_session || s_play) &&
            (!recorder_storage_ready() || recorder_storage_generation()!=s_storage_generation)) {
            s_play=false; close_session(); publish(MEDIA_ERROR,"SD 卡已移除，请重新选择音乐");
        }
        if ((s_session || s_play) && (audio_device_voice_waiting() || cloud_ota_busy())) {
            s_play = false; close_session();
            publish(MEDIA_PAUSED, "语音或更新使用中，结束后可重新播放");
        }
        if (!s_play) { vTaskDelay(pdMS_TO_TICKS(25)); continue; }
        if (!s_session && !open_session()) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
        playback_step();
    }
}

esp_err_t media_service_start(void)
{
    nvs_handle_t handle;
    if (nvs_open("media", NVS_READONLY, &handle) == ESP_OK) {
        uint8_t volume;
        if (nvs_get_u8(handle, "volume", &volume) == ESP_OK && volume <= 100) s_volume = volume;
        nvs_close(handle);
    }
    s_commands = xQueueCreate(8, sizeof(media_request_t));
    s_jobs = xQueueCreate(1, sizeof(fetch_job_t));
    s_stream_storage = heap_caps_malloc(STREAM_CAPACITY + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_commands || !s_jobs || !s_stream_storage) return ESP_ERR_NO_MEM;
    s_stream = xStreamBufferCreateStatic(STREAM_CAPACITY + 1, 1, s_stream_storage, &s_stream_control);
    if (!s_stream) return ESP_ERR_NO_MEM;
    publish(MEDIA_IDLE, "选择音乐开始播放");
    if (xTaskCreate(fetch_task, "media_sd", 8192, NULL, 3, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    if (xTaskCreateWithCaps(playback_task, "media_play", 8192, NULL, 4, NULL,
                           MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}
