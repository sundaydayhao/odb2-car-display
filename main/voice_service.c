#include "voice_service.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "esp_opus_enc.h"
#include "esp_opus_dec.h"
#include "esp_xiaozhi_chat.h"
#include "esp_xiaozhi_info.h"
#include "esp_timer.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "cloud_service.h"
#include "device_services.h"

#define OPUS_MAX 1400
#define PCM_MAX 4096
typedef struct { uint16_t length; uint32_t epoch; uint8_t data[OPUS_MAX]; } audio_packet_t;
static QueueHandle_t s_audio;
static atomic_bool s_pressed, s_active, s_accept_audio, s_fault, s_channel;
static atomic_uint s_press_id, s_epoch;
static esp_xiaozhi_chat_handle_t s_chat;
static esp_codec_dev_handle_t s_mic, s_speaker;
static int s_codec_mode;

void voice_set_pressed(bool pressed)
{
    if (pressed && !atomic_exchange(&s_pressed, true)) atomic_fetch_add(&s_press_id, 1);
    if (!pressed) atomic_store(&s_pressed, false);
}

bool voice_is_active(void) { return atomic_load(&s_active); }

static bool codec_mode(int mode)
{
    if (s_codec_mode == mode) return true;
    if (s_codec_mode) esp_codec_dev_close(s_codec_mode == 1 ? s_mic : s_speaker);
    s_codec_mode = 0;
    if (!mode) return true;
    esp_codec_dev_sample_info_t format = {.sample_rate = 16000, .channel = 1, .bits_per_sample = 16};
    esp_codec_dev_handle_t codec = mode == 1 ? s_mic : s_speaker;
    if (!codec || esp_codec_dev_open(codec, &format) != ESP_CODEC_DEV_OK) return false;
    s_codec_mode = mode;
    if (mode == 1) esp_codec_dev_set_in_gain(codec, 24.0f);
    else esp_codec_dev_set_out_vol(codec, 55);
    return true;
}

static void audio_in(const uint8_t *data, int len, void *ctx)
{
    (void)ctx;
    if (!atomic_load(&s_accept_audio) || atomic_load(&s_pressed)) return;
    if (len <= 0 || len > OPUS_MAX) { atomic_store(&s_fault, true); return; }
    audio_packet_t packet = {.length = len, .epoch = atomic_load(&s_epoch)};
    memcpy(packet.data, data, len);
    if (xQueueSend(s_audio, &packet, 0) != pdTRUE) atomic_store(&s_fault, true);
}

static void chat_event(esp_xiaozhi_chat_event_t event, void *data, void *ctx)
{
    (void)data;
    (void)ctx;
    if (event == ESP_XIAOZHI_CHAT_EVENT_CHAT_ERROR) atomic_store(&s_fault, true);
    /* Server system commands and firmware URLs are deliberately not executed. */
}

static void transport_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)data;
    if (id == ESP_XIAOZHI_CHAT_EVENT_AUDIO_CHANNEL_OPENED) atomic_store(&s_channel, true);
    if (id == ESP_XIAOZHI_CHAT_EVENT_DISCONNECTED || id == ESP_XIAOZHI_CHAT_EVENT_AUDIO_CHANNEL_CLOSED ||
        id == ESP_XIAOZHI_CHAT_EVENT_SERVER_GOODBYE) {
        atomic_store(&s_channel, false);
        atomic_store(&s_accept_audio, false);
    }
}

static bool connect_chat(void)
{
    if (s_chat && atomic_load(&s_channel)) return true;
    device_status_set(SERVICE_VOICE, SERVICE_BUSY, "正在连接语音服务");
    if (!s_chat) {
        esp_xiaozhi_chat_info_t info = {0};
        esp_err_t err = esp_xiaozhi_chat_get_info(&info);
        if (err != ESP_OK) {
            esp_xiaozhi_chat_free_info(&info);
            device_status_set(SERVICE_VOICE, SERVICE_ERROR, "语音服务连接失败");
            return false;
        }
        if (!info.has_mqtt_config && !info.has_websocket_config) {
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
        esp_xiaozhi_chat_free_info(&info);
        err = esp_xiaozhi_chat_init(&config, &s_chat);
        if (err == ESP_OK) err = esp_xiaozhi_chat_start(s_chat);
        if (err != ESP_OK) {
            if (s_chat) esp_xiaozhi_chat_deinit(s_chat);
            s_chat = 0;
            device_status_set(SERVICE_VOICE, SERVICE_ERROR, "语音会话启动失败");
            return false;
        }
    }
    esp_xiaozhi_chat_audio_t format = {.format = "opus", .sample_rate = 16000, .channels = 1, .frame_duration = 60};
    if (esp_xiaozhi_chat_open_audio_channel(s_chat, &format, NULL, 0) != ESP_OK) {
        device_status_set(SERVICE_VOICE, SERVICE_ERROR, "语音通道连接失败");
        return false;
    }
    atomic_store(&s_channel, true);
    return true;
}

static void end_chat(void)
{
    atomic_store(&s_accept_audio, false);
    atomic_store(&s_active, false);
    atomic_fetch_add(&s_epoch, 1);
    xQueueReset(s_audio);
    codec_mode(0);
    if (s_chat) esp_xiaozhi_chat_deinit(s_chat);
    s_chat = 0;
    atomic_store(&s_channel, false);
}

static void voice_task(void *arg)
{
    (void)arg;
    void *encoder = NULL, *decoder = NULL;
    uint8_t *pcm = malloc(PCM_MAX), *encoded = malloc(OPUS_MAX);
    esp_opus_enc_config_t enc = ESP_OPUS_ENC_CONFIG_DEFAULT();
    enc.sample_rate = 16000; enc.channel = 1; enc.bitrate = 24000;
    enc.frame_duration = ESP_OPUS_ENC_FRAME_DURATION_60_MS;
    esp_opus_dec_cfg_t dec = {.sample_rate = 16000, .channel = 1,
        .frame_duration = ESP_OPUS_DEC_FRAME_DURATION_60_MS, .self_delimited = false};
    int input_size = 0, output_size = 0;
    if (!pcm || !encoded || esp_opus_enc_open(&enc, sizeof(enc), &encoder) != ESP_AUDIO_ERR_OK ||
        esp_opus_enc_get_frame_size(encoder, &input_size, &output_size) != ESP_AUDIO_ERR_OK ||
        input_size > PCM_MAX || output_size > OPUS_MAX ||
        esp_opus_dec_open(&dec, sizeof(dec), &decoder) != ESP_AUDIO_ERR_OK) goto failed;
    uint32_t handled_press = 0;
    bool listening = false;
    int discard = 0;
    int64_t last_activity = 0, listen_start = 0;
    while (1) {
        bool pressed = atomic_load(&s_pressed);
        uint32_t press_id = atomic_load(&s_press_id);
        bool fault = atomic_exchange(&s_fault, false);
        if (cloud_ota_busy() || (s_chat && !device_network_ready()) || fault) {
            listening = false;
            handled_press = press_id;
            end_chat();
            device_status_set(SERVICE_VOICE, fault ? SERVICE_ERROR : SERVICE_WAITING,
                              fault ? "语音链路异常" : "语音已暂停");
        }
        if (pressed && press_id != handled_press && !cloud_ota_busy()) {
            handled_press = press_id;
            if (listening) {
                esp_xiaozhi_chat_send_stop_listening(s_chat);
                listening = false;
            }
            if (!device_network_ready() || !device_clock_ready()) {
                device_status_set(SERVICE_VOICE, SERVICE_WAITING, "等待联网与时间同步");
                continue;
            }
            atomic_store(&s_accept_audio, false);
            atomic_fetch_add(&s_epoch, 1);
            xQueueReset(s_audio);
            codec_mode(0);
            atomic_store(&s_active, true);
            bool connected = connect_chat();
            if (!connected) { end_chat(); continue; }
            if (!s_mic) {
                s_speaker = bsp_audio_codec_speaker_init();
                s_mic = bsp_audio_codec_microphone_init();
            }
            last_activity = esp_timer_get_time();
            /* A release during HTTPS/hello must never result in delayed recording. */
            if (!atomic_load(&s_pressed) || press_id != atomic_load(&s_press_id)) {
                atomic_store(&s_active, false);
                device_status_set(SERVICE_VOICE, SERVICE_READY, "语音已就绪");
                continue;
            }
            esp_xiaozhi_chat_send_abort_speaking(s_chat, ESP_XIAOZHI_CHAT_ABORT_SPEAKING_REASON_STOP_LISTENING);
            if (!codec_mode(1) || esp_xiaozhi_chat_send_start_listening(s_chat,
                    ESP_XIAOZHI_CHAT_LISTENING_MODE_MANUAL) != ESP_OK) {
                end_chat();
                device_status_set(SERVICE_VOICE, SERVICE_ERROR, "麦克风或会话不可用");
                continue;
            }
            listening = true;
            discard = 3;
            listen_start = last_activity;
            device_status_set(SERVICE_VOICE, SERVICE_BUSY, "正在聆听");
        }
        if (listening && (!atomic_load(&s_pressed) || !atomic_load(&s_channel) ||
                          esp_timer_get_time() - listen_start > 30000000)) {
            listening = false;
            codec_mode(0);
            atomic_store(&s_accept_audio, true);
            esp_xiaozhi_chat_send_stop_listening(s_chat);
            last_activity = esp_timer_get_time();
            device_status_set(SERVICE_VOICE, SERVICE_BUSY, "等待语音回复");
        }
        if (listening) {
            if (esp_codec_dev_read(s_mic, pcm, input_size) != ESP_CODEC_DEV_OK) {
                atomic_store(&s_fault, true); continue;
            }
            if (discard) { discard--; continue; }
            esp_audio_enc_in_frame_t in = {.buffer = pcm, .len = input_size};
            esp_audio_enc_out_frame_t out = {.buffer = encoded, .len = OPUS_MAX};
            if (esp_opus_enc_process(encoder, &in, &out) != ESP_AUDIO_ERR_OK) {
                atomic_store(&s_fault, true); continue;
            }
            if (atomic_load(&s_pressed) && atomic_load(&s_press_id) == handled_press && out.encoded_bytes &&
                esp_xiaozhi_chat_send_audio_data(s_chat, (const char *)encoded, out.encoded_bytes) != ESP_OK)
                atomic_store(&s_fault, true);
            last_activity = esp_timer_get_time();
        } else {
            audio_packet_t packet;
            if (xQueueReceive(s_audio, &packet, pdMS_TO_TICKS(30)) == pdTRUE &&
                packet.epoch == atomic_load(&s_epoch) && !atomic_load(&s_pressed) && atomic_load(&s_accept_audio)) {
                esp_audio_dec_in_raw_t raw = {.buffer = packet.data, .len = packet.length};
                esp_audio_dec_out_frame_t out = {.buffer = pcm, .len = PCM_MAX};
                esp_audio_dec_info_t info = {0};
                if (esp_opus_dec_decode(decoder, &raw, &out, &info) != ESP_AUDIO_ERR_OK ||
                    raw.consumed != packet.length || !codec_mode(2) ||
                    esp_codec_dev_write(s_speaker, pcm, out.decoded_size) != ESP_CODEC_DEV_OK) {
                    atomic_store(&s_fault, true);
                } else {
                    device_status_set(SERVICE_VOICE, SERVICE_BUSY, "正在播放回复");
                    last_activity = esp_timer_get_time();
                }
            }
            if (s_chat && esp_timer_get_time() - last_activity > 30000000) {
                end_chat();
                device_status_set(SERVICE_VOICE, SERVICE_READY, "语音已就绪");
            }
        }
    }
failed:
    if (encoder) esp_opus_enc_close(encoder);
    if (decoder) esp_opus_dec_close(decoder);
    free(pcm); free(encoded);
    device_status_set(SERVICE_VOICE, SERVICE_ERROR, "音频资源不足");
    vTaskDelete(NULL);
}

esp_err_t voice_service_start(void)
{
    s_audio = xQueueCreate(24, sizeof(audio_packet_t));
    if (!s_audio) return ESP_ERR_NO_MEM;
    esp_err_t err = esp_event_handler_register(ESP_XIAOZHI_CHAT_EVENTS, ESP_EVENT_ANY_ID, transport_event, NULL);
    if (err != ESP_OK) return err;
    device_status_set(SERVICE_VOICE, SERVICE_OFF, "语音待命");
    return xTaskCreate(voice_task, "voice", 12288, NULL, 5, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
