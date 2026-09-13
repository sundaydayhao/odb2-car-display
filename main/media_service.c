#include "media_service.h"
#include "audio_device.h"
#include "device_services.h"
#include "cloud_service.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
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
typedef struct { unsigned generation; char url[MEDIA_URL_SIZE]; } fetch_job_t;
typedef struct { uint32_t metadata_interval; bool unsupported; } headers_t;
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
static char s_custom[2][MEDIA_URL_SIZE];
/* Only the playback task owns the following session fields and decoder. */
static unsigned s_source, s_volume = 35, s_frames, s_bad_frames;
static bool s_play, s_session, s_audio_owned, s_buffering;
static void *s_decoder;
static uint8_t *s_input, *s_pcm;
static size_t s_used, s_skip;
static unsigned s_junk;
static uint64_t s_played_us;
static int64_t s_retry_at, s_volume_save_at;

static void publish(media_state_t state, const char *detail)
{
    portENTER_CRITICAL(&s_lock);
    s_status.state = state;
    s_status.source = s_source;
    s_status.volume = s_volume;
    s_status.played_ms = (uint32_t)(s_played_us / 1000);
    s_status.decoded_frames = s_frames;
    s_status.received_bytes = atomic_load(&s_received);
    s_status.buffered_bytes = s_stream ? xStreamBufferBytesAvailable(s_stream) : 0;
    s_status.custom_music = *s_custom[0] != 0;
    s_status.custom_radio = *s_custom[1] != 0;
    snprintf(s_status.title, sizeof(s_status.title), "%s", media_sources[s_source].title);
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
void media_service_custom_urls(char music[MEDIA_URL_SIZE], char radio[MEDIA_URL_SIZE])
{
    portENTER_CRITICAL(&s_lock);
    memcpy(music, s_custom[0], MEDIA_URL_SIZE);
    memcpy(radio, s_custom[1], MEDIA_URL_SIZE);
    portEXIT_CRITICAL(&s_lock);
}
bool media_service_request(const media_request_t *request)
{
    if (!request || !s_commands || request->action > MEDIA_CUSTOM_SAVE) return false;
    if (request->action == MEDIA_SELECT && request->value >= MEDIA_SOURCE_COUNT) return false;
    if (request->action == MEDIA_VOLUME && request->value > 100) return false;
    if (request->action == MEDIA_CUSTOM_SAVE && (request->value > 1 ||
        !memchr(request->url, 0, MEDIA_URL_SIZE) || (*request->url && !media_url_valid(request->url)))) return false;
    return xQueueSend(s_commands, request, 0) == pdTRUE;
}

static esp_err_t header_event(esp_http_client_event_t *event)
{
    headers_t *headers = event->user_data;
    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key && event->header_value) {
        if (!strcasecmp(event->header_key, "icy-metaint")) {
            unsigned long value = strtoul(event->header_value, NULL, 10);
            if (value && value <= 16 * 1024 * 1024) headers->metadata_interval = value;
        }
        if (!strcasecmp(event->header_key, "Content-Type")) {
            const char *type = event->header_value;
            headers->unsupported = !strncasecmp(type, "text/", 5) || strstr(type, "mpegurl") ||
                strstr(type, "aac") || strstr(type, "mp4") || strstr(type, "ogg");
        }
    }
    return ESP_OK;
}
static bool cancelled(unsigned generation)
{
    return atomic_load(&s_generation) != generation || !device_network_ready() || cloud_ota_busy();
}
static void fetch_task(void *arg)
{
    (void)arg;
    uint8_t *block = heap_caps_malloc(2048, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    fetch_job_t job;
    while (xQueueReceive(s_jobs, &job, portMAX_DELAY) == pdTRUE) {
        int error = 0;
        headers_t headers = {0};
        esp_http_client_config_t config = {.url = job.url, .timeout_ms = 6000,
            .buffer_size = 2048, .buffer_size_tx = 512, .disable_auto_redirect = true,
            .crt_bundle_attach = esp_crt_bundle_attach, .event_handler = header_event, .user_data = &headers};
        esp_http_client_handle_t client = block ? esp_http_client_init(&config) : NULL;
        if (!client) error = 1;
        if (client) {
            esp_http_client_set_header(client, "Icy-MetaData", "1");
            esp_http_client_set_header(client, "User-Agent", "VehicleMedia/0.2.6");
            bool opened = false;
            for (int redirect = 0; redirect < 6 && !cancelled(job.generation); ++redirect) {
                headers = (headers_t){0};
                if (esp_http_client_open(client, 0) != ESP_OK || esp_http_client_fetch_headers(client) < 0) break;
                int code = esp_http_client_get_status_code(client);
                if (code == 200) { opened = true; break; }
                if (code != 301 && code != 302 && code != 303 && code != 307 && code != 308) break;
                if (esp_http_client_set_redirection(client) != ESP_OK) break;
                char redirected[MEDIA_URL_SIZE];
                if (esp_http_client_get_url(client, redirected, sizeof(redirected)) != ESP_OK || !media_url_valid(redirected)) break;
                esp_http_client_close(client);
            }
            if (!opened) error = 2;
            else if (headers.unsupported) error = 3;
            else {
                esp_http_client_set_timeout_ms(client, 2000);
                media_icy_t icy;
                media_icy_init(&icy, headers.metadata_interval);
                unsigned read_timeouts = 0;
                while (!cancelled(job.generation)) {
                    int received = esp_http_client_read(client, (char *)block, 2048);
                    /* Live servers can deliver several seconds in one burst.
                     * EAGAIN is a polling timeout, not the end of the stream;
                     * keep the connection and decoder until 10 s without data. */
                    if (received == -ESP_ERR_HTTP_EAGAIN && ++read_timeouts < 5) continue;
                    if (received < 0) { error = 2; break; }
                    if (!received) {
                        if (!esp_http_client_is_complete_data_received(client)) error = 2;
                        break;
                    }
                    read_timeouts = 0;
                    atomic_fetch_add(&s_received, (unsigned)received);
                    size_t length = media_icy_filter(&icy, block, received), offset = 0;
                    while (offset < length && !cancelled(job.generation))
                        offset += xStreamBufferSend(s_stream, block + offset, length - offset, pdMS_TO_TICKS(50));
                }
            }
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
        }
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
    /* The reader owns its HTTP client. Wait for its cancellation acknowledgement
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
    case MEDIA_SELECT:
        close_session();
        s_source = request->value; s_play = true; s_retry_at = 0;
        s_played_us = 0; s_frames = s_bad_frames = 0;
        publish(MEDIA_CONNECTING, "正在准备播放");
        break;
    case MEDIA_TOGGLE:
        s_play = !s_play; s_retry_at = 0;
        if (s_play && !s_session) { s_played_us = 0; s_frames = s_bad_frames = 0; }
        if (!s_play) {
            if (media_sources[s_source].kind == MEDIA_RADIO) close_session();
            else if (s_audio_owned) { audio_device_release(AUDIO_OWNER_MEDIA); s_audio_owned = false; }
            publish(MEDIA_PAUSED, media_sources[s_source].kind == MEDIA_RADIO ? "已暂停，继续将收听实时直播" : "已暂停");
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
    case MEDIA_CUSTOM_SAVE: {
        nvs_handle_t handle;
        esp_err_t err = nvs_open("media", NVS_READWRITE, &handle);
        if (err == ESP_OK) {
            err = nvs_set_str(handle, request->value ? "radio" : "music", request->url);
            if (err == ESP_OK) err = nvs_commit(handle);
            nvs_close(handle);
        }
        if (err == ESP_OK) {
            portENTER_CRITICAL(&s_lock);
            memcpy(s_custom[request->value], request->url, MEDIA_URL_SIZE);
            portEXIT_CRITICAL(&s_lock);
            if (s_source == (request->value ? MEDIA_CUSTOM_RADIO : MEDIA_CUSTOM_MUSIC)) {
                s_play = false; close_session();
            }
        }
        publish(s_play ? s_status.state : MEDIA_IDLE, err == ESP_OK ? "播放地址已保存" : "播放地址保存失败");
        break;
    }
    }
}

static bool open_session(void)
{
    if (atomic_load(&s_fetch_busy)) return false;
    const char *url = s_source == MEDIA_CUSTOM_MUSIC ? s_custom[0] :
                      s_source == MEDIA_CUSTOM_RADIO ? s_custom[1] : media_sources[s_source].url;
    if (!*url) { s_play = false; publish(MEDIA_ERROR, "请先在设置中保存播放地址"); return false; }
    if (!strncmp(url, "https://", 8) && !device_clock_ready()) {
        publish(MEDIA_CONNECTING, "等待网络校时"); return false;
    }
    if (esp_mp3_dec_open(NULL, 0, &s_decoder) != ESP_AUDIO_ERR_OK) {
        s_play = false; publish(MEDIA_ERROR, "音乐解码资源不足"); return false;
    }
    fetch_job_t job = {.generation = atomic_fetch_add(&s_generation, 1) + 1};
    snprintf(job.url, sizeof(job.url), "%s", url);
    xStreamBufferReset(s_stream);
    atomic_store(&s_fetch_done, false); atomic_store(&s_fetch_error, 0); atomic_store(&s_received, 0);
    atomic_store(&s_fetch_busy, true);
    if (xQueueSend(s_jobs, &job, 0) != pdTRUE) {
        atomic_store(&s_fetch_busy, false); close_session();
        s_play = false; publish(MEDIA_ERROR, "播放任务繁忙，请重试"); return false;
    }
    s_session = true; s_buffering = true; s_used = s_skip = s_junk = 0; s_bad_frames = 0;
    publish(MEDIA_CONNECTING, "正在连接音源");
    return true;
}
static void failed(const char *detail, bool retry)
{
    close_session();
    s_play = retry;
    if (retry && media_sources[s_source].kind == MEDIA_MUSIC) { s_played_us = 0; s_frames = 0; }
    s_retry_at = esp_timer_get_time() + 3000000;
    publish(retry ? MEDIA_CONNECTING : MEDIA_ERROR, detail);
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
        if (!s_used && done && !available) { failed("音乐标签数据不完整", false); return; }
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
        if (tag < 0) { failed("音乐标签格式不受支持", false); return; }
        if (!tag && done && !xStreamBufferBytesAvailable(s_stream)) { failed("音乐标签数据不完整", false); return; }
        if (!tag) { s_buffering = true; vTaskDelay(pdMS_TO_TICKS(5)); }
        if (tag > 0) s_skip = tag;
        return;
    }
    int frame_size = media_mp3_frame_size(s_input, s_used);
    if (frame_size > 0 && (size_t)frame_size <= s_used) {
        esp_audio_dec_in_raw_t raw = {.buffer = s_input, .len = frame_size};
        esp_audio_dec_out_frame_t frame = {.buffer = s_pcm, .len = PCM_CAPACITY};
        esp_audio_dec_info_t info = {0};
        esp_audio_err_t err = esp_mp3_dec_decode(s_decoder, &raw, &frame, &info);
        memmove(s_input, s_input + frame_size, s_used - frame_size); s_used -= frame_size;
        if (err != ESP_AUDIO_ERR_OK || raw.consumed != (unsigned)frame_size || !frame.decoded_size) {
            if (++s_bad_frames > 12) failed("音频无法解码，请更换音源", false);
            return;
        }
        s_bad_frames = 0; s_junk = 0;
        if (info.bits_per_sample != 16 || (info.channel != 1 && info.channel != 2) ||
            info.sample_rate < 8000 || info.sample_rate > 48000 || frame.decoded_size > PCM_CAPACITY ||
            frame.decoded_size % (2 * info.channel)) { failed("音频采样格式不受支持", false); return; }
        size_t samples = frame.decoded_size / 2 / info.channel;
        int16_t *pcm = (int16_t *)s_pcm;
        if (info.channel == 2)
            for (size_t i = 0; i < samples; ++i) pcm[i] = ((int32_t)pcm[2*i] + pcm[2*i+1]) / 2;
        if (!s_audio_owned) s_audio_owned = audio_device_acquire(AUDIO_OWNER_MEDIA);
        if (!s_audio_owned) { failed("音频正在被语音使用，请稍后播放", false); return; }
        if (!audio_device_configure(AUDIO_OWNER_MEDIA, false, info.sample_rate, s_volume) ||
            !audio_device_write(AUDIO_OWNER_MEDIA, pcm, samples * 2)) { failed("扬声器不可用，请检查音频连接", false); return; }
        ++s_frames; s_played_us += samples * 1000000ULL / info.sample_rate;
        if (s_frames == 1 || !(s_frames % 8)) publish(MEDIA_PLAYING, media_sources[s_source].kind == MEDIA_RADIO ? "正在收听网络直播" : "正在播放音乐");
        return;
    }
    if (frame_size < 0 && s_used) {
        memmove(s_input, s_input + 1, --s_used);
        if (++s_junk > 65536) failed("需要 MP3 音频直链，不支持网页或播放清单", false);
        return;
    }
    if (done && !xStreamBufferBytesAvailable(s_stream)) {
        int error = atomic_load(&s_fetch_error);
        bool radio = media_sources[s_source].kind == MEDIA_RADIO;
        if (error == 3) failed("音源格式不支持，请使用 MP3 直链", false);
        else if (error || radio) failed("音源暂时中断，正在重新连接", true);
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
        if ((s_session || s_play) && (audio_device_voice_waiting() || cloud_ota_busy())) {
            s_play = false; close_session();
            publish(MEDIA_PAUSED, "语音或更新使用中，结束后可重新播放");
        }
        if (!s_play) { vTaskDelay(pdMS_TO_TICKS(25)); continue; }
        if (!device_network_ready()) {
            if (s_session) close_session();
            publish(MEDIA_CONNECTING, "等待连接 Wi-Fi 热点");
            vTaskDelay(pdMS_TO_TICKS(100)); continue;
        }
        if (esp_timer_get_time() < s_retry_at) { vTaskDelay(pdMS_TO_TICKS(25)); continue; }
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
        for (unsigned i = 0; i < 2; ++i) {
            size_t size = MEDIA_URL_SIZE;
            if (nvs_get_str(handle, i ? "radio" : "music", s_custom[i], &size) != ESP_OK ||
                (*s_custom[i] && !media_url_valid(s_custom[i]))) s_custom[i][0] = 0;
        }
        nvs_close(handle);
    }
    s_commands = xQueueCreate(8, sizeof(media_request_t));
    s_jobs = xQueueCreate(1, sizeof(fetch_job_t));
    s_stream_storage = heap_caps_malloc(STREAM_CAPACITY + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_commands || !s_jobs || !s_stream_storage) return ESP_ERR_NO_MEM;
    s_stream = xStreamBufferCreateStatic(STREAM_CAPACITY + 1, 1, s_stream_storage, &s_stream_control);
    if (!s_stream) return ESP_ERR_NO_MEM;
    publish(MEDIA_IDLE, "选择音乐或电台开始播放");
    if (xTaskCreate(fetch_task, "media_http", 8192, NULL, 3, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    if (xTaskCreateWithCaps(playback_task, "media_play", 8192, NULL, 4, NULL,
                           MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}
