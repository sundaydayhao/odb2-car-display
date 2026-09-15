#include "voice_service.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "esp_opus_enc.h"
#include "esp_opus_dec.h"
#include "esp_xiaozhi_chat.h"
#include "esp_xiaozhi_info.h"
#include "esp_timer.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "cloud_service.h"
#include "device_services.h"
#include "audio_device.h"
#include "voice_frontend.h"
#include "voice_codec_memory.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "nvs.h"

#define OPUS_MAX 1400
#define PCM_MAX 4096
typedef struct { uint16_t length; uint32_t epoch; uint8_t data[OPUS_MAX]; } audio_packet_t;
static QueueHandle_t s_audio;
static atomic_bool s_active, s_accept_audio, s_fault, s_connected, s_channel, s_tts_started, s_tts_stopped;
static atomic_bool s_enabled=true, s_save_enabled, s_manual_start, s_manual_end;
static atomic_uint s_epoch;
static esp_xiaozhi_chat_handle_t s_chat;
/* esp_xiaozhi 0.1.2 requires an MCP engine even for audio-only sessions.
 * Keep ownership here so failed chat initialization and normal teardown share
 * the same cleanup path; no device-control tools are registered. */
static esp_mcp_t *s_mcp;
static atomic_uintptr_t s_voice_task_handle;
static bool s_output_owned;
static void *s_encoder, *s_decoder;
static uint8_t *s_pcm, *s_encoded;
static int s_input_size, s_output_size;
static int64_t s_next_send_at;
static atomic_uint s_tx_frames, s_rx_frames, s_played_frames, s_user_texts, s_tts_events;
static atomic_uint s_capture_ms, s_speech_ms, s_frame_count, s_frame_abs_sum, s_peak;
static atomic_uint s_turn;
static atomic_bool s_server_goodbye;
static atomic_llong s_wake_at_us;
static atomic_llong s_first_audio_ms = -1, s_first_pcm_ms = -1, s_first_signal_ms = -1;
static atomic_llong s_wake_encode_ms, s_wake_upload_ms;
static int64_t s_encode_us, s_upload_us;
ESP_EVENT_DEFINE_BASE(VOICE_INTERNAL_EVENTS);
enum { CONTROL_DRAIN_REQUIRED, CONTROL_DRAIN_PENDING, CONTROL_DRAIN_READY };
static atomic_int s_control_drain_state = CONTROL_DRAIN_READY;

static bool drain_control_events(void)
{
    int expected=CONTROL_DRAIN_REQUIRED;
    if (atomic_compare_exchange_strong(&s_control_drain_state,&expected,CONTROL_DRAIN_PENDING)) {
        if (esp_event_post(VOICE_INTERNAL_EVENTS,0,NULL,0,pdMS_TO_TICKS(100))!=ESP_OK) {
            /* A full event queue must not permanently block the next wake. */
            atomic_store(&s_control_drain_state,CONTROL_DRAIN_REQUIRED);
            return false;
        }
    }
    int64_t deadline=esp_timer_get_time()+1000000;
    while (atomic_load(&s_control_drain_state)==CONTROL_DRAIN_PENDING && esp_timer_get_time()<deadline)
        vTaskDelay(pdMS_TO_TICKS(1));
    return atomic_load(&s_control_drain_state)==CONTROL_DRAIN_READY;
}

static int64_t wake_elapsed_ms(void)
{
    int64_t wake = atomic_load(&s_wake_at_us);
    return wake ? (esp_timer_get_time()-wake)/1000 : -1;
}
static void log_wake_latency(const char *stage)
{
    int64_t elapsed=wake_elapsed_ms();
    if (elapsed>=0) ESP_LOGI("voice", "Wake latency: %s=%lld ms", stage,(long long)elapsed);
}

void voice_log_diagnostics(void)
{
    unsigned frames=atomic_load(&s_frame_count);
    ESP_LOGI("voice","Audio statistics: tx=%u rx=%u played=%u recognized=%u tts=%u capture_ms=%u speech_ms=%u mean_abs=%u peak=%u stack_free=%lu turn=%u",
        atomic_load(&s_tx_frames),atomic_load(&s_rx_frames),atomic_load(&s_played_frames),
        atomic_load(&s_user_texts),atomic_load(&s_tts_events),atomic_load(&s_capture_ms),atomic_load(&s_speech_ms),
        frames ? atomic_load(&s_frame_abs_sum)/frames : 0,atomic_load(&s_peak),(unsigned long)voice_stack_min_free(),atomic_load(&s_turn));
    if (atomic_load(&s_wake_at_us))
        ESP_LOGI("voice","Wake timing summary: encode=%lld upload=%lld first_audio=%lld first_pcm_written=%lld first_signal_pcm_written=%lld ms",
            (long long)atomic_load(&s_wake_encode_ms),(long long)atomic_load(&s_wake_upload_ms),
            (long long)atomic_load(&s_first_audio_ms),(long long)atomic_load(&s_first_pcm_ms),
            (long long)atomic_load(&s_first_signal_ms));
}

static void reset_statistics(void)
{
    atomic_store(&s_tx_frames,0); atomic_store(&s_rx_frames,0); atomic_store(&s_played_frames,0);
    atomic_store(&s_user_texts,0); atomic_store(&s_tts_events,0); atomic_store(&s_capture_ms,0);
    atomic_store(&s_speech_ms,0); atomic_store(&s_frame_count,0); atomic_store(&s_frame_abs_sum,0);
    atomic_store(&s_peak,0);
}

bool voice_is_active(void) { return atomic_load(&s_active); }
bool voice_is_enabled(void) { return atomic_load(&s_enabled); }
void voice_set_enabled(bool enabled)
{
    if (atomic_exchange(&s_enabled,enabled)!=enabled) atomic_store(&s_save_enabled,true);
    if (!enabled) { atomic_store(&s_manual_start,false); atomic_store(&s_accept_audio,false); }
}
bool voice_begin_conversation(void)
{
    if (!voice_is_enabled() || voice_is_active()) return false;
    atomic_store(&s_manual_start,true); return true;
}
void voice_end_conversation(void)
{
    atomic_store(&s_manual_start,false); atomic_store(&s_manual_end,true);
    atomic_store(&s_accept_audio,false);
}
uint32_t voice_stack_min_free(void)
{
    TaskHandle_t task = (TaskHandle_t)atomic_load(&s_voice_task_handle);
    return task ? uxTaskGetStackHighWaterMark(task) : 0;
}

static esp_err_t ensure_client_identity(void)
{
    /* In esp_xiaozhi 0.1.2, a missing board/uuid reads as a successful empty
     * default, so its generator is skipped. Seed that key before get_info;
     * otherwise the server issues an MQTT client ID with an empty suffix. */
    nvs_handle_t handle;
    esp_err_t err = nvs_open("board", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    char uuid[37] = {0};
    size_t size = sizeof(uuid);
    err = nvs_get_str(handle, "uuid", uuid, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND || (err == ESP_OK && !uuid[0])) {
        uint8_t bytes[16];
        esp_fill_random(bytes, sizeof(bytes));
        bytes[6] = (bytes[6] & 0x0f) | 0x40;
        bytes[8] = (bytes[8] & 0x3f) | 0x80;
        snprintf(uuid, sizeof(uuid), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                 bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
                 bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
        err = nvs_set_str(handle, "uuid", uuid);
        if (err == ESP_OK) err = nvs_commit(handle);
        if (err == ESP_OK) ESP_LOGI("voice", "Persistent client identity created");
    }
    nvs_close(handle);
    return err;
}

static void audio_in(const uint8_t *data, int len, void *ctx)
{
    (void)ctx;
    /* Snapshot before the gate: an in-flight callback must keep its old turn.
     * Like official Speaking, only TTS start enables reply audio. A delayed
     * UDP tail from the previous reply must not stop the next microphone turn. */
    uint32_t epoch = atomic_load(&s_epoch);
    if (!atomic_load(&s_accept_audio) || !atomic_load(&s_tts_started)) return;
    if (!data || len <= 0 || len > OPUS_MAX) { atomic_store(&s_fault, true); return; }
    if (atomic_fetch_add(&s_rx_frames,1)==0) {
        ESP_LOGI("voice","First reply audio packet received");
        if (atomic_load(&s_turn)<=1) {
            /* A callback already past the gate can overlap the next turn's
             * counter reset. Preserve this wake's first measurement once set. */
            long long unset=-1, elapsed=wake_elapsed_ms();
            if (elapsed>=0 && atomic_compare_exchange_strong(&s_first_audio_ms,&unset,elapsed))
                ESP_LOGI("voice","Wake latency: first_audio=%lld ms",elapsed);
        }
    }
    audio_packet_t packet = {.length = len, .epoch = epoch};
    memcpy(packet.data, data, len);
    if (xQueueSend(s_audio, &packet, 0) != pdTRUE) atomic_store(&s_fault, true);
}
static void chat_event(esp_xiaozhi_chat_event_t event, void *data, void *ctx)
{
    (void)ctx;
    if (event == ESP_XIAOZHI_CHAT_EVENT_CHAT_ERROR) atomic_store(&s_fault, true);
    if (event == ESP_XIAOZHI_CHAT_EVENT_CHAT_TEXT && data) {
        const esp_xiaozhi_chat_text_data_t *text=data;
        if (text->role==ESP_XIAOZHI_CHAT_TEXT_ROLE_USER) {
            atomic_fetch_add(&s_user_texts,1);
            ESP_LOGI("voice","Server recognized user speech (%u bytes)",text->text ? (unsigned)strlen(text->text) : 0);
        }
    }
    if (event == ESP_XIAOZHI_CHAT_EVENT_CHAT_TTS_STATE && data) {
        esp_xiaozhi_chat_tts_state_t *tts = data;
        atomic_fetch_add(&s_tts_events,1);
        ESP_LOGI("voice","Server TTS state: %d",(int)tts->state);
        if (tts->state == ESP_XIAOZHI_CHAT_TTS_STATE_START) {
            atomic_store(&s_tts_stopped, false);
            atomic_store(&s_tts_started, true);
        } else if (tts->state == ESP_XIAOZHI_CHAT_TTS_STATE_STOP) atomic_store(&s_tts_stopped, true);
    }
    /* Server system commands and firmware URLs are not executed. */
}
static void transport_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)data;
    if (base == VOICE_INTERNAL_EVENTS) { atomic_store(&s_control_drain_state,CONTROL_DRAIN_READY); return; }
    if (id==ESP_XIAOZHI_CHAT_EVENT_SERVER_GOODBYE) atomic_store(&s_server_goodbye,true);
    if (id == ESP_XIAOZHI_CHAT_EVENT_CONNECTED) atomic_store(&s_connected, true);
    if (id == ESP_XIAOZHI_CHAT_EVENT_DISCONNECTED) atomic_store(&s_connected, false);
    /* Open/close events are asynchronous echoes of our own API calls, with no
     * instance identifier. The voice task owns that state at the API boundary. */
    if (id == ESP_XIAOZHI_CHAT_EVENT_DISCONNECTED ||
        id == ESP_XIAOZHI_CHAT_EVENT_SERVER_GOODBYE) {
        atomic_store(&s_channel, false);
        atomic_store(&s_accept_audio, false);
    }
}

static bool prepare_control(bool foreground)
{
    if (s_chat && atomic_load(&s_connected)) return true;
    if (!drain_control_events()) return false;
    int64_t started = esp_timer_get_time();
    if (foreground) device_status_set(SERVICE_VOICE, SERVICE_BUSY, "正在连接语音服务");
    if (!s_chat) {
        esp_err_t err = ensure_client_identity();
        if (err != ESP_OK) {
            ESP_LOGE("voice", "Client identity initialization failed: %s", esp_err_to_name(err));
            device_status_set(SERVICE_VOICE, SERVICE_ERROR, "语音会话启动失败");
            return false;
        }
        esp_xiaozhi_chat_info_t info = {0};
        err = esp_xiaozhi_chat_get_info(&info);
        if (err != ESP_OK) {
            ESP_LOGE("voice", "Service discovery failed: %s", esp_err_to_name(err));
            esp_xiaozhi_chat_free_info(&info);
            device_status_set(SERVICE_VOICE, SERVICE_ERROR, "语音服务连接失败");
            return false;
        }
        ESP_LOGI("voice", "Service configuration: mqtt=%d websocket=%d activation=%d",
                 info.has_mqtt_config, info.has_websocket_config, info.has_activation_code);
        /* Unbound devices may receive placeholder transports together with an
         * activation code. Binding takes priority over connecting those. */
        if (info.has_activation_code || (!info.has_mqtt_config && !info.has_websocket_config)) {
            char detail[96] = "请先绑定小智设备";
            if (info.has_activation_code && info.activation_code) {
                char code[17] = {0};
                size_t n = 0;
                while (n < 16 && info.activation_code[n] >= '0' && info.activation_code[n] <= '9') {
                    code[n] = info.activation_code[n]; n++;
                }
                snprintf(detail, sizeof(detail), "绑定码 %s", code);
            }
            device_status_set(SERVICE_VOICE, SERVICE_WAITING, detail);
            esp_xiaozhi_chat_free_info(&info);
            return false;
        }
        esp_xiaozhi_chat_config_t config = ESP_XIAOZHI_CHAT_DEFAULT_CONFIG();
        config.audio_callback = audio_in;
        config.event_callback = chat_event;
        config.has_mqtt_config = info.has_mqtt_config;
        config.has_websocket_config = info.has_websocket_config;
        ESP_LOGI("voice","Selected official transport: %s",config.has_mqtt_config ? "mqtt+udp" : "websocket");
        esp_xiaozhi_chat_free_info(&info);
        err = esp_mcp_create(&s_mcp);
        if (err != ESP_OK) {
            ESP_LOGE("voice", "MCP initialization failed: %s", esp_err_to_name(err));
            device_status_set(SERVICE_VOICE, SERVICE_ERROR, "语音会话启动失败");
            return false;
        }
        config.mcp_engine = s_mcp;
        config.owns_mcp_engine = false;
        atomic_store(&s_connected, false);
        err = esp_xiaozhi_chat_init(&config, &s_chat);
        if (err == ESP_OK) err = esp_xiaozhi_chat_start(s_chat);
        if (err != ESP_OK) {
            ESP_LOGE("voice", "Chat startup failed: %s", esp_err_to_name(err));
            /* The caller tears down all callbacks through end_chat(). */
            device_status_set(SERVICE_VOICE, SERVICE_ERROR, "语音会话启动失败");
            return false;
        }
    }
    /* Starting MQTT/WebSocket launches an asynchronous connection. Sending the
     * QoS 0 hello before CONNECTED loses it and makes every first wake fail. */
    int64_t deadline = esp_timer_get_time() + 10000000;
    while (!atomic_load(&s_connected) && !atomic_load(&s_fault) &&
           device_network_ready() && !cloud_ota_busy() && esp_timer_get_time() < deadline)
        vTaskDelay(pdMS_TO_TICKS(20));
    if (!atomic_load(&s_connected) || atomic_load(&s_fault) || !device_network_ready() || cloud_ota_busy()) {
        ESP_LOGE("voice", "Transport connection did not become ready");
        device_status_set(SERVICE_VOICE, SERVICE_ERROR, "语音服务连接失败");
        return false;
    }
    ESP_LOGI("voice", "Control connection prepared: %lld ms; background=%d",
             (long long)((esp_timer_get_time()-started)/1000), !foreground);
    return true;
}

static bool connect_chat(void)
{
    if (s_chat && atomic_load(&s_channel)) return true;
    if (!prepare_control(true)) return false;
    log_wake_latency("control_ready");
    device_status_set(SERVICE_VOICE, SERVICE_BUSY, "正在连接语音服务");
    esp_xiaozhi_chat_audio_t format = {.format = "opus", .sample_rate = 16000, .channels = 1, .frame_duration = 60};
    esp_err_t err = esp_xiaozhi_chat_open_audio_channel(s_chat, &format, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE("voice", "Audio channel open failed: %s", esp_err_to_name(err));
        device_status_set(SERVICE_VOICE, SERVICE_ERROR, "语音通道连接失败");
        return false;
    }
    atomic_store(&s_channel, true);
    ESP_LOGI("voice", "Audio channel ready");
    log_wake_latency("audio_channel");
    return true;
}

static void end_chat(void)
{
    if (voice_is_active()) voice_log_diagnostics();
    voice_frontend_pause();
    atomic_store(&s_accept_audio, false);
    /* Stop callbacks before discarding queued reply audio and resetting flags. */
    if (s_chat) {
        esp_xiaozhi_chat_deinit(s_chat);
        /* Old transport events carry no instance id. Drain them after stopping
         * their source, before any background connection creates a new client. */
        atomic_store(&s_control_drain_state,CONTROL_DRAIN_REQUIRED);
        if (!drain_control_events()) ESP_LOGE("voice","Control event drain not completed");
    }
    s_chat = 0;
    if (s_mcp) esp_mcp_destroy(s_mcp);
    s_mcp = NULL;
    atomic_fetch_add(&s_epoch, 1);
    xQueueReset(s_audio);
    atomic_store(&s_channel, false);
    atomic_store(&s_connected, false);
    atomic_store(&s_tts_started, false);
    atomic_store(&s_tts_stopped, false);
    atomic_store(&s_server_goodbye, false);
    if (s_output_owned) audio_device_release(AUDIO_OWNER_VOICE);
    s_output_owned = false;
    audio_device_voice_priority(false);
    atomic_store(&s_active, false);
    if (s_encoder) esp_opus_enc_close(s_encoder);
    if (s_decoder) esp_opus_dec_close(s_decoder);
    s_encoder = s_decoder = NULL;
    free(s_pcm); free(s_encoded);
    s_pcm = s_encoded = NULL;
    if (voice_is_enabled()) voice_frontend_arm(); else voice_frontend_pause();
}

static bool prepare_audio(void)
{
    if (s_encoder && s_decoder) return true;
    int64_t started=esp_timer_get_time();
    s_pcm = heap_caps_malloc(PCM_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_encoded = heap_caps_malloc(OPUS_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    esp_opus_enc_config_t enc = ESP_OPUS_ENC_CONFIG_DEFAULT();
    enc.sample_rate = 16000; enc.channel = 1; enc.bitrate = 24000;
    enc.frame_duration = ESP_OPUS_ENC_FRAME_DURATION_60_MS;
    esp_opus_dec_cfg_t dec = {.sample_rate = 16000, .channel = 1,
        .frame_duration = ESP_OPUS_DEC_FRAME_DURATION_60_MS, .self_delimited = false};
    bool ready=false;
    if (s_pcm && s_encoded) {
        /* Prefer internal RAM only for this task's encoder state allocation.
         * End the scope before checking errors or creating another codec. */
        bool scoped=voice_codec_memory_begin();
        esp_audio_err_t opened=esp_opus_enc_open(&enc,sizeof(enc),&s_encoder);
        if (scoped) voice_codec_memory_end();
        ready=opened == ESP_AUDIO_ERR_OK &&
        esp_opus_enc_get_frame_size(s_encoder, &s_input_size, &s_output_size) == ESP_AUDIO_ERR_OK &&
        s_input_size == 1920 && s_output_size > 0 && s_output_size <= OPUS_MAX &&
        esp_opus_dec_open(&dec, sizeof(dec), &s_decoder) == ESP_AUDIO_ERR_OK;
    }
    ESP_LOGI("voice","Codecs prepared: %lld ms; ready=%d",
             (long long)((esp_timer_get_time()-started)/1000),ready);
    return ready;
}

/* Accumulate the AFE's 32 ms frames into the protocol's 60 ms Opus frames. */
static bool send_pcm(const int16_t *pcm, size_t samples, size_t *used, bool pace)
{
    const uint8_t *bytes = (const uint8_t *)pcm;
    for (size_t left = samples * 2; left;) {
        size_t n = s_input_size - *used;
        if (n > left) n = left;
        memcpy(s_pcm + *used, bytes, n);
        *used += n; bytes += n; left -= n;
        if (*used == (size_t)s_input_size) {
            esp_audio_enc_in_frame_t in = {.buffer = s_pcm, .len = s_input_size};
            esp_audio_enc_out_frame_t out = {.buffer = s_encoded, .len = OPUS_MAX};
            int64_t started=esp_timer_get_time();
            esp_audio_err_t encoded=esp_opus_enc_process(s_encoder, &in, &out);
            s_encode_us+=esp_timer_get_time()-started;
            if (encoded != ESP_AUDIO_ERR_OK || !out.encoded_bytes) {
                ESP_LOGE("voice","Opus encoding failed"); return false;
            }
            /* Service discovery can leave seconds of queued microphone audio.
             * Preserve it, but do not burst UDP packets faster than their audio duration. */
            int64_t delay=pace ? s_next_send_at-esp_timer_get_time() : 0;
            if (delay>0) vTaskDelay(pdMS_TO_TICKS((delay+999)/1000));
            if (!device_network_ready() || !atomic_load(&s_channel) || atomic_load(&s_fault)) return false;
            started=esp_timer_get_time();
            esp_err_t sent=esp_xiaozhi_chat_send_audio_data(s_chat,(const char *)s_encoded,out.encoded_bytes);
            s_upload_us+=esp_timer_get_time()-started;
            if (sent!=ESP_OK) { ESP_LOGE("voice","Audio upload failed: %s",esp_err_to_name(sent)); return false; }
            s_next_send_at=esp_timer_get_time()+60000;
            atomic_fetch_add(&s_tx_frames,1);
            *used = 0;
        }
    }
    return true;
}
static bool send_wake_detected(size_t *used)
{
    if (*used) {
        int16_t silence[960] = {0};
        if (!send_pcm(silence,(s_input_size-*used)/2,used,false)) return false;
    }
    ESP_LOGI("voice","Wake audio uploaded: frames=%u; sending official listen/detect",atomic_load(&s_tx_frames));
    atomic_store(&s_wake_encode_ms,s_encode_us/1000);
    atomic_store(&s_wake_upload_ms,s_upload_us/1000);
    ESP_LOGI("voice","Wake preparation: encode=%lld ms upload=%lld ms",
             (long long)(s_encode_us/1000),(long long)(s_upload_us/1000));
    log_wake_latency("detect");
    return esp_xiaozhi_chat_send_wake_word(s_chat,VOICE_WAKE_WORD)==ESP_OK;
}
static bool acquire_output(void)
{
    if (!s_output_owned) {
        for (int i=0;i<100 && !cloud_ota_busy();++i) {
            if (audio_device_acquire(AUDIO_OWNER_VOICE)) { s_output_owned=true; break; }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
    return s_output_owned && audio_device_configure(AUDIO_OWNER_VOICE,false,16000,55);
}
static void release_output(void)
{
    if (s_output_owned) audio_device_release(AUDIO_OWNER_VOICE);
    s_output_owned=false;
}
static bool begin_listening(size_t *used, bool new_turn)
{
    /* A new turn reuses the transport/session and its packet sequence. Only
     * local per-turn buffers and flags are cleared. The initial detect may
     * already be delivering greeting audio, so it must preserve that queue. */
    if (new_turn) {
        atomic_store(&s_accept_audio,false);
        atomic_fetch_add(&s_epoch,1);
        xQueueReset(s_audio);
        atomic_store(&s_tts_started,false);
        atomic_store(&s_tts_stopped,false);
        reset_statistics();
    }
    atomic_fetch_add(&s_turn,1);
    *used=0; s_next_send_at=0;
    atomic_store(&s_accept_audio,true);
    if (esp_xiaozhi_chat_send_start_listening(s_chat,ESP_XIAOZHI_CHAT_LISTENING_MODE_AUTO)!=ESP_OK) return false;
    if (new_turn) voice_frontend_listen();
    device_status_set(SERVICE_VOICE,SERVICE_BUSY,"正在聆听，可以继续说");
    ESP_LOGI("voice","Official auto listening started: turn=%u",atomic_load(&s_turn));
    return true;
}
static bool play_packet(const audio_packet_t *packet)
{
    if (!acquire_output()) return false;
    esp_audio_dec_in_raw_t raw = {.buffer = (uint8_t *)packet->data, .len = packet->length};
    esp_audio_dec_out_frame_t out = {.buffer = s_pcm, .len = PCM_MAX};
    esp_audio_dec_info_t info = {0};
    if (esp_opus_dec_decode(s_decoder, &raw, &out, &info) != ESP_AUDIO_ERR_OK ||
        raw.consumed != packet->length || !out.decoded_size || out.decoded_size > PCM_MAX ||
        info.sample_rate != 16000 || info.channel != 1 || info.bits_per_sample != 16) return false;
    /* Diagnostic only: distinguish a received packet from decoded speech
     * energy. Keep all PCM, including silence; this does not trim playback.
     * It is a software output marker, not a speaker acoustic measurement. */
    unsigned peak=0, sum=0, samples=out.decoded_size/sizeof(int16_t);
    bool measure_signal=atomic_load(&s_turn)==1 && atomic_load(&s_first_signal_ms)<0;
    if (measure_signal) for (unsigned i=0;i<samples;++i) {
        int32_t sample=((int16_t *)s_pcm)[i];
        unsigned value=sample<0 ? -sample : sample;
        if (value>peak) peak=value;
        sum+=value;
    }
    bool played=audio_device_write(AUDIO_OWNER_VOICE,s_pcm,out.decoded_size);
    if (played) {
        if (atomic_fetch_add(&s_played_frames,1)==0 && atomic_load(&s_turn)==1) {
            atomic_store(&s_first_pcm_ms,wake_elapsed_ms());
            log_wake_latency("first_pcm_written");
        }
        if (measure_signal && peak>=256 && samples && sum/samples>=32) {
            atomic_store(&s_first_signal_ms,wake_elapsed_ms());
            log_wake_latency("first_signal_pcm_written");
        }
    }
    else ESP_LOGE("voice","Reply audio output failed");
    return played;
}

static void voice_task(void *arg)
{
    (void)arg;
    esp_err_t err = voice_frontend_start();
    if (err != ESP_OK) {
        ESP_LOGE("voice", "Wake frontend initialization failed: %s", esp_err_to_name(err));
        device_status_set(SERVICE_VOICE, SERVICE_ERROR, "唤醒初始化失败，请检查音频设备");
        vTaskDelete(NULL); return;
    }
    atomic_store(&s_voice_task_handle, (uintptr_t)xTaskGetCurrentTaskHandle());
    device_status_set(SERVICE_VOICE, SERVICE_READY, "请说：" VOICE_WAKE_WORD);
    bool listening = false, paused = false, invoking_wake=false, warm_attempted=false, disabled=false;
    size_t used = 0;
    int64_t last_activity = 0;
    voice_frame_t frame;
    for (;;) {
        if (atomic_exchange(&s_save_enabled,false)) {
            nvs_handle_t nvs;
            if (nvs_open("voice_ui",NVS_READWRITE,&nvs)==ESP_OK) {
                esp_err_t result=nvs_set_u8(nvs,"enabled",voice_is_enabled());
                if (result==ESP_OK) result=nvs_commit(nvs);
                if (result!=ESP_OK) ESP_LOGW("voice","Assistant setting save: %s",esp_err_to_name(result));
                nvs_close(nvs);
            }
        }
        if (!voice_is_enabled()) {
            if (!disabled) {
                end_chat(); voice_frontend_set_enabled(false); listening=false; invoking_wake=false; disabled=true;
                device_status_set(SERVICE_VOICE,SERVICE_OFF,"语音助手已关闭");
                ESP_LOGI("voice","Assistant disabled; wake detection and microphone processing stopped");
            }
            atomic_store(&s_manual_end,false);
            vTaskDelay(pdMS_TO_TICKS(100)); continue;
        }
        if (disabled) {
            voice_frontend_set_enabled(true); voice_frontend_arm(); disabled=false; warm_attempted=false;
            device_status_set(SERVICE_VOICE,SERVICE_READY,"请说：" VOICE_WAKE_WORD);
        }
        if (atomic_exchange(&s_manual_end,false)) {
            end_chat(); listening=false; invoking_wake=false; warm_attempted=false;
            device_status_set(SERVICE_VOICE,SERVICE_READY,"对话已结束，等待唤醒");
            continue;
        }
        bool fault = atomic_exchange(&s_fault, false) || voice_frontend_failed();
        if (!device_network_ready()) warm_attempted=false;
        if (cloud_ota_busy()) {
            if (!paused) {
                end_chat(); voice_frontend_pause(); paused = true; listening = false;
                device_status_set(SERVICE_VOICE, SERVICE_WAITING, "更新期间暂停语音唤醒");
            }
            vTaskDelay(pdMS_TO_TICKS(100)); continue;
        }
        if (paused) {
            paused = false; warm_attempted=false; voice_frontend_arm();
            device_status_set(SERVICE_VOICE, SERVICE_READY, "请说：" VOICE_WAKE_WORD);
        }
        if (atomic_load(&s_server_goodbye)) {
            ESP_LOGI("voice","Server ended conversation; returning to wake standby");
            bool was_active=voice_is_active();
            end_chat(); listening=false; warm_attempted=!was_active;
            device_status_set(SERVICE_VOICE,SERVICE_READY,"请说：" VOICE_WAKE_WORD);
            continue;
        }
        if (fault || (s_chat && (!device_network_ready() || !atomic_load(&s_connected) ||
                                 (voice_is_active() && !atomic_load(&s_channel))))) {
            end_chat(); listening = false;
            device_status_set(SERVICE_VOICE, SERVICE_ERROR, "语音连接中断，请重新唤醒");
            continue;
        }
        if (!voice_is_active()) {
            /* Move HTTPS/TLS and codec allocation out of the wake path. No
             * audio hello or microphone upload occurs during preparation.
             * One background attempt per online/normal-idle transition; an
             * explicit wake may retry a failed preparation. */
            if (!warm_attempted && device_network_ready() && device_clock_ready()) {
                warm_attempted=true;
                if (!prepare_control(false) || !prepare_audio()) { end_chat(); continue; }
                ESP_LOGI("voice","Wake standby prepared; microphone upload remains off");
                device_status_set(SERVICE_VOICE,SERVICE_READY,"请说：" VOICE_WAKE_WORD);
            }
            bool manual=atomic_exchange(&s_manual_start,false);
            if (manual) { memset(&frame,0,sizeof(frame)); frame.wake_at_us=esp_timer_get_time(); voice_frontend_pause(); }
            else if (!voice_frontend_receive(&frame, pdMS_TO_TICKS(100)) || !frame.wake) continue;
            if (!device_network_ready() || !device_clock_ready()) {
                voice_frontend_arm();
                device_status_set(SERVICE_VOICE, SERVICE_WAITING, "请先联网，再说你好小智");
                continue;
            }
            atomic_store(&s_active, true);
            atomic_store(&s_wake_at_us,frame.wake_at_us);
            atomic_store(&s_first_audio_ms,-1); atomic_store(&s_first_pcm_ms,-1);
            atomic_store(&s_first_signal_ms,-1);
            atomic_store(&s_wake_encode_ms,0); atomic_store(&s_wake_upload_ms,0);
            s_encode_us=s_upload_us=0;
            warm_attempted=true;
            atomic_store(&s_turn,0);
            reset_statistics();
            audio_device_voice_priority(true);
            if (!connect_chat()) { end_chat(); continue; }
            if (!prepare_audio()) {
                end_chat();
                device_status_set(SERVICE_VOICE, SERVICE_ERROR, "语音内存不足，请重新唤醒");
                continue;
            }
            if (!voice_is_enabled() || atomic_load(&s_manual_end) || cloud_ota_busy() || !device_network_ready() || !atomic_load(&s_channel) ||
                voice_frontend_failed() || atomic_load(&s_fault)) {
                end_chat();
                device_status_set(SERVICE_VOICE, SERVICE_ERROR, "连接超时，请重新唤醒");
                continue;
            }
            /* Match official SEND_WAKE_WORD_DATA: send the actual pre-roll,
             * then listen/detect, then listen/start(auto). This both reports a
             * real wake and establishes the outgoing UDP audio path first. */
            atomic_store(&s_accept_audio,true);
            used=0; s_next_send_at=0; listening=true; invoking_wake=!manual;
            if (manual && !begin_listening(&used,true)) { atomic_store(&s_fault,true); continue; }
            last_activity=esp_timer_get_time();
        }
        if (listening && !invoking_wake && atomic_load(&s_tts_started)) {
            /* Stop uploads before decoding into the shared PCM workspace. */
            voice_frontend_pause(); used = 0; listening = false;
            invoking_wake=false; last_activity=esp_timer_get_time();
            /* Official Speaking entry resets the Opus decoder for a new TTS stream. */
            esp_opus_dec_close(s_decoder); s_decoder=NULL;
            esp_opus_dec_cfg_t decoder={.sample_rate=16000,.channel=1,
                .frame_duration=ESP_OPUS_DEC_FRAME_DURATION_60_MS,.self_delimited=false};
            if (esp_opus_dec_open(&decoder,sizeof(decoder),&s_decoder)!=ESP_AUDIO_ERR_OK) {
                atomic_store(&s_fault,true); continue;
            }
        }
        if (listening) {
            if (voice_frontend_receive(&frame, pdMS_TO_TICKS(100)) && !frame.wake && frame.samples) {
                uint32_t sum=0,peak=0;
                for (unsigned i=0;i<frame.samples;++i) {
                    unsigned value=frame.pcm[i]<0 ? -(int32_t)frame.pcm[i] : frame.pcm[i];
                    sum+=value; if (value>peak) peak=value;
                }
                atomic_fetch_add(&s_frame_count,1);
                atomic_fetch_add(&s_frame_abs_sum,sum/frame.samples);
                if (peak>atomic_load(&s_peak)) atomic_store(&s_peak,peak);
                atomic_fetch_add(&s_capture_ms,frame.samples/16);
                if (frame.speech) atomic_fetch_add(&s_speech_ms,frame.samples/16);
                if (!send_pcm(frame.pcm, frame.samples, &used,!frame.preroll)) { atomic_store(&s_fault, true); continue; }
                last_activity = esp_timer_get_time();
                if (invoking_wake && frame.wake_end) {
                    if (!send_wake_detected(&used) || !begin_listening(&used,false)) {
                        atomic_store(&s_fault,true); continue;
                    }
                    invoking_wake=false;
                }
                /* In official auto mode the server owns VAD and idle expiry.
                 * Do not cut off microphone audio using a local silence timer. */
            } else if (esp_timer_get_time() - last_activity > 2000000) atomic_store(&s_fault, true);
        } else if (voice_is_active()) {
            audio_packet_t packet;
            if (xQueueReceive(s_audio, &packet, pdMS_TO_TICKS(30)) == pdTRUE &&
                packet.epoch == atomic_load(&s_epoch)) {
                if (!play_packet(&packet)) { atomic_store(&s_fault, true); continue; }
                last_activity = esp_timer_get_time();
                device_status_set(SERVICE_VOICE, SERVICE_BUSY, "正在播放回复");
            }
            int64_t now = esp_timer_get_time();
            /* A stop event may arrive before the last UDP packet. Allow 600 ms
             * for reordering and drain the I2S queue before resuming capture. */
            if (atomic_load(&s_tts_stopped) && !uxQueueMessagesWaiting(s_audio) && now-last_activity>600000) {
                voice_log_diagnostics();
                release_output();
                if (atomic_load(&s_server_goodbye) || !device_network_ready() || !atomic_load(&s_channel)) continue;
                /* DMA drain yields to callbacks; preserve any new reply that
                 * began during it instead of clearing its start and packets. */
                if (!atomic_load(&s_tts_stopped) || uxQueueMessagesWaiting(s_audio)) continue;
                if (!begin_listening(&used,true)) { atomic_store(&s_fault,true); continue; }
                listening=true; last_activity=esp_timer_get_time();
            }
        }
    }
}

esp_err_t voice_service_start(void)
{
    nvs_handle_t nvs;
    if (nvs_open("voice_ui",NVS_READONLY,&nvs)==ESP_OK) {
        uint8_t enabled;
        if (nvs_get_u8(nvs,"enabled",&enabled)==ESP_OK) atomic_store(&s_enabled,enabled!=0);
        nvs_close(nvs);
    }
    s_audio = xQueueCreate(24, sizeof(audio_packet_t));
    if (!s_audio) return ESP_ERR_NO_MEM;
    esp_err_t err = esp_event_handler_register(ESP_XIAOZHI_CHAT_EVENTS, ESP_EVENT_ANY_ID, transport_event, NULL);
    if (err != ESP_OK) return err;
    err=esp_event_handler_register(VOICE_INTERNAL_EVENTS,ESP_EVENT_ANY_ID,transport_event,NULL);
    if (err!=ESP_OK) return err;
    device_status_set(SERVICE_VOICE, SERVICE_WAITING, "正在加载语音唤醒");
    /* esp_audio_codec recommends about 40 KiB for encoder stacks. The 60 ms
     * Opus SILK path overflowed the former 12 KiB task on its first frame.
     * Include room for this service's buffers; keep the stack in internal RAM
     * because service discovery also performs NVS/flash operations. */
    return xTaskCreate(voice_task, "voice", 48 * 1024, NULL, 5, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
