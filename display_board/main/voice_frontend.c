#include "voice_frontend.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include "audio_device.h"
#include "cloud_service.h"
#include "esp_afe_sr_models.h"
#include "esp_afe_sr_iface.h"
#include "esp_ae_rate_cvt.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/queue.h"
#include "freertos/task.h"

enum { FRONTEND_PAUSED, FRONTEND_ARMED, FRONTEND_CAPTURE };
/* 32 seconds covers the HTTPS binding/connection timeout without dropping the
 * question spoken immediately after the wake word. All storage is bounded. */
#define FRAME_QUEUE_SIZE 1024
/* AFE emits 32 ms mono frames: 63 frames retain about two seconds, including
 * the frame that triggered WakeNet. Only fetch_task owns this bounded ring. */
#define WAKE_PREROLL_FRAMES 63
static QueueHandle_t s_frames;
static StaticQueue_t s_frames_control;
static uint8_t *s_frames_storage;
static voice_frame_t *s_preroll;
/* Generation and mode share one atomic word: a detector finishing an old
 * fetch must never change the mode of a newly armed session. */
static atomic_uint s_state;
static atomic_bool s_fault;
static atomic_bool s_enabled=true;
static atomic_llong s_arm_at;
static const esp_afe_sr_iface_t *s_afe;
static esp_afe_sr_data_t *s_afe_data;
static srmodel_list_t *s_models;
static esp_ae_rate_cvt_handle_t s_resample;
static int16_t *s_raw, *s_converted, *s_feed;
static unsigned s_feed_samples;
static uint32_t s_converted_cap;
extern const uint8_t voice_models_start[] asm("_binary_voice_models_bin_start");

void voice_frontend_pause(void)
{
    unsigned state = atomic_load(&s_state);
    while (!atomic_compare_exchange_weak(&s_state, &state, ((state >> 2) + 1) << 2)) {}
    if (s_frames) xQueueReset(s_frames);
}
void voice_frontend_arm(void)
{
    voice_frontend_pause();
    atomic_store(&s_arm_at, esp_timer_get_time() + 1500000);
    atomic_fetch_or(&s_state, FRONTEND_ARMED);
}
void voice_frontend_set_enabled(bool enabled)
{
    voice_frontend_pause(); atomic_store(&s_enabled,enabled);
}
void voice_frontend_listen(void)
{
    voice_frontend_pause();
    atomic_fetch_or(&s_state, FRONTEND_CAPTURE);
}
bool voice_frontend_receive(voice_frame_t *frame, TickType_t wait)
{
    if (xQueueReceive(s_frames, frame, wait) != pdTRUE) return false;
    return frame->generation == atomic_load(&s_state) >> 2;
}
bool voice_frontend_failed(void) { return atomic_exchange(&s_fault, false); }

static bool enqueue_frame(const voice_frame_t *frame)
{
    if (frame->generation != atomic_load(&s_state) >> 2) return false;
    if (xQueueSend(s_frames, frame, 0) == pdTRUE) return true;
    atomic_store(&s_fault, true);
    return false;
}

static void feed_task(void *arg)
{
    (void)arg;
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    unsigned used = 0;
    for (;;) {
        if (!atomic_load(&s_enabled)) { used=0; vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        /* Fixed 10 ms blocks, stereo microphone/reference, 48 -> 16 kHz. */
        if (!audio_device_capture(s_raw, 480)) {
            atomic_store(&s_fault, true);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        uint32_t count = s_converted_cap;
        if (esp_ae_rate_cvt_process(s_resample, s_raw, 480, s_converted, &count) != ESP_AE_ERR_OK) {
            atomic_store(&s_fault, true); continue;
        }
        for (unsigned offset = 0; offset < count;) {
            unsigned n = count - offset;
            if (n > s_feed_samples - used) n = s_feed_samples - used;
            memcpy(s_feed + used * 2, s_converted + offset * 2, n * 4);
            used += n; offset += n;
            if (used == s_feed_samples) {
                s_afe->feed(s_afe_data, s_feed);
                used = 0;
            }
        }
    }
}
/* Only fetch_task changes AFE controls. Keep AEC/VAD and the audio stream
 * running while WakeNet is disabled outside wake standby. */
static bool apply_wakenet_state(unsigned state, int *applied)
{
    int enabled = (state & 3) == FRONTEND_ARMED;
    if (*applied == enabled) return true;
    int result = enabled ? s_afe->enable_wakenet(s_afe_data) : s_afe->disable_wakenet(s_afe_data);
    if (result != enabled) {
        *applied = -1;
        atomic_store(&s_fault, true);
        ESP_LOGE("voice", "WakeNet control failed: requested=%d result=%d", enabled, result);
        return false;
    }
    *applied = enabled;
    ESP_LOGI("voice", "WakeNet enabled=%d mode=%u", enabled, state & 3);
    return true;
}

static void fetch_task(void *arg)
{
    (void)arg;
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    voice_frame_t frame;
    unsigned timeouts = 0;
    int wakenet_enabled = -1;
    unsigned history_generation = 0, history_next = 0, history_count = 0;
    for (;;) {
        unsigned state = atomic_load(&s_state);
        if (!apply_wakenet_state(state, &wakenet_enabled)) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (!atomic_load(&s_enabled)) { timeouts=0; vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        if (history_generation != state >> 2) {
            /* A pause/rearm invalidates the old ring without letting another
             * task modify its storage while a wake snapshot is being queued. */
            history_generation = state >> 2;
            history_next = history_count = 0;
        }
        afe_fetch_result_t *result = s_afe->fetch_with_delay(s_afe_data, pdMS_TO_TICKS(100));
        if (!result || result->ret_value == ESP_FAIL) {
            if (++timeouts >= 10) { atomic_store(&s_fault, true); timeouts = 0; }
            continue;
        }
        timeouts = 0;
        if (state != atomic_load(&s_state) || cloud_ota_busy()) continue;
        int mode = state & 3;
        if (mode == FRONTEND_PAUSED) continue;
        if (!result->data || result->data_size <= 0 ||
            (size_t)result->data_size > sizeof(frame.pcm) || result->data_size % 2) {
            atomic_store(&s_fault, true); continue;
        }
        memset(&frame, 0, sizeof(frame));
        frame.generation = state >> 2;
        frame.samples = result->data_size / sizeof(int16_t);
        frame.speech = result->vad_state == VAD_SPEECH;
        /* Keep silence too: VAD is metadata, never a PCM trimming gate. */
        memcpy(frame.pcm, result->data, result->data_size);
        if (mode == FRONTEND_ARMED) {
            s_preroll[history_next] = frame;
            history_next = (history_next + 1) % WAKE_PREROLL_FRAMES;
            if (history_count < WAKE_PREROLL_FRAMES) ++history_count;
            if (esp_timer_get_time() < atomic_load(&s_arm_at) || result->wakeup_state != WAKENET_DETECTED) continue;
            unsigned expected = state;
            if (!atomic_compare_exchange_strong(&s_state, &expected, (state & ~3u) | FRONTEND_CAPTURE)) continue;
            int64_t wake_at = esp_timer_get_time();
            /* Stop further wake inference before handing the saved audio to
             * the voice task, so it does not compete with Opus encoding. */
            if (!apply_wakenet_state((state & ~3u) | FRONTEND_CAPTURE, &wakenet_enabled)) continue;
            memset(&frame, 0, sizeof(frame));
            frame.generation = state >> 2;
            frame.wake = true;
            frame.wake_at_us = wake_at;
            ESP_LOGI("voice", "Wake word detected: %s; preroll_frames=%u", VOICE_WAKE_WORD, history_count);
            if (!enqueue_frame(&frame)) continue;
            unsigned first = (history_next + WAKE_PREROLL_FRAMES - history_count) % WAKE_PREROLL_FRAMES;
            for (unsigned i = 0; i < history_count; ++i) {
                frame = s_preroll[(first + i) % WAKE_PREROLL_FRAMES];
                frame.preroll = true;
                frame.wake_end = i + 1 == history_count;
                if (!enqueue_frame(&frame)) break;
            }
            continue;
        }
        enqueue_frame(&frame);
    }
}

esp_err_t voice_frontend_start(void)
{
    if (!audio_device_init()) return ESP_FAIL;
    esp_err_t error = ESP_ERR_NO_MEM;
    s_models = srmodel_load(voice_models_start);
    if (!s_models || !esp_srmodel_filter(s_models, ESP_WN_PREFIX, "nihaoxiaozhi")) { error = ESP_ERR_NOT_FOUND; goto fail; }
    afe_config_t *cfg = afe_config_init("MR", s_models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    if (!cfg) goto fail;
    cfg->wakenet_model_name = esp_srmodel_filter(s_models, ESP_WN_PREFIX, "nihaoxiaozhi");
    cfg->vad_model_name = esp_srmodel_filter(s_models, ESP_VADN_PREFIX, NULL);
    cfg->aec_init = true;
    cfg->se_init = false;
    cfg->wakenet_init = true;
    cfg->vad_init = true;
    cfg->vad_min_noise_ms = 128;
    cfg->vad_min_speech_ms = 128;
    cfg->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    cfg->afe_perferred_core = 1;
    cfg->afe_perferred_priority = 5;
    s_afe = esp_afe_handle_from_config(cfg);
    s_afe_data = s_afe ? s_afe->create_from_config(cfg) : NULL;
    afe_config_free(cfg);
    if (!s_afe_data) goto fail;
    s_feed_samples = s_afe->get_feed_chunksize(s_afe_data);
    if (!s_feed_samples || s_feed_samples > 1024 || s_afe->get_feed_channel_num(s_afe_data) != 2 ||
        s_afe->get_fetch_chunksize(s_afe_data) > VOICE_FRAME_SAMPLES) { error = ESP_ERR_INVALID_SIZE; goto fail; }
    esp_ae_rate_cvt_cfg_t resample = {.src_rate = 48000, .dest_rate = 16000, .channel = 2,
        .bits_per_sample = 16, .complexity = 2, .perf_type = ESP_AE_RATE_CVT_PERF_TYPE_MEMORY};
    if (esp_ae_rate_cvt_open(&resample, &s_resample) != ESP_AE_ERR_OK) goto fail;
    if (esp_ae_rate_cvt_get_max_out_sample_num(s_resample, 480, &s_converted_cap) != ESP_AE_ERR_OK ||
        s_converted_cap > 1024) { error = ESP_ERR_INVALID_SIZE; goto fail; }
    s_raw = heap_caps_malloc(480 * 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_converted = heap_caps_malloc(s_converted_cap * 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_feed = heap_caps_malloc(s_feed_samples * 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_frames_storage = heap_caps_malloc(FRAME_QUEUE_SIZE * sizeof(voice_frame_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_preroll = heap_caps_malloc(WAKE_PREROLL_FRAMES * sizeof(voice_frame_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_raw || !s_converted || !s_feed || !s_frames_storage || !s_preroll) goto fail;
    s_frames = xQueueCreateStatic(FRAME_QUEUE_SIZE, sizeof(voice_frame_t), s_frames_storage, &s_frames_control);
    if (!s_frames) goto fail;
    TaskHandle_t fetch = NULL, feed = NULL;
    if (xTaskCreatePinnedToCore(fetch_task, "voice_detect", 6144, NULL, 5, &fetch, 1) != pdPASS) goto fail;
    if (xTaskCreatePinnedToCore(feed_task, "voice_feed", 6144, NULL, 6, &feed, 0) != pdPASS) {
        vTaskDelete(fetch); goto fail;
    }
    voice_frontend_arm();
    xTaskNotifyGive(fetch);
    xTaskNotifyGive(feed);
    return ESP_OK;
fail:
    if (s_frames) vQueueDelete(s_frames);
    s_frames = NULL;
    free(s_frames_storage); s_frames_storage = NULL;
    free(s_preroll); s_preroll = NULL;
    free(s_raw); free(s_converted); free(s_feed);
    s_raw = s_converted = s_feed = NULL;
    if (s_resample) esp_ae_rate_cvt_close(s_resample);
    s_resample = NULL;
    if (s_afe_data) s_afe->destroy(s_afe_data);
    s_afe_data = NULL;
    if (s_models) esp_srmodel_deinit(s_models);
    s_models = NULL;
    return error;
}
