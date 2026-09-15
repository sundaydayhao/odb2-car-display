#include "audio_device.h"
#include <stdatomic.h>
#include <stdlib.h>
#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_ae_rate_cvt.h"
#include "esp_heap_caps.h"

/* Keep the ADC/DAC clock running at 48 kHz. Reopening the codec per utterance
 * interrupts wake detection. ES8311 left ADC is mic, right is DAC reference
 * (no_dac_ref=false); the speech frontend uses both for AEC. */
#define HW_RATE 48000
#define OUT_CAP 1600
static atomic_int s_owner;
static atomic_bool s_voice_wanted, s_ready;
static esp_codec_dev_handle_t s_codec;
static esp_ae_rate_cvt_handle_t s_converter;
static unsigned s_rate;
static int s_volume = -1;
static int16_t *s_mono, *s_stereo;

bool audio_device_init(void)
{
    if (atomic_load(&s_ready)) return true;
    /* Called once by voice startup, before output can acquire. */
    i2s_chan_handle_t tx = NULL, rx = NULL;
    const audio_codec_data_if_t *data = NULL;
    const audio_codec_ctrl_if_t *ctrl = NULL;
    const audio_codec_gpio_if_t *gpio = NULL;
    const audio_codec_if_t *codec = NULL;
    if (bsp_i2c_init() != ESP_OK) return false;
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(CONFIG_BSP_I2S_NUM, I2S_ROLE_MASTER);
    channel.dma_desc_num = 6;
    channel.dma_frame_num = 480;
    channel.auto_clear = true;
    if (i2s_new_channel(&channel, &tx, &rx) != ESP_OK) goto fail;
    i2s_std_config_t bus = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(HW_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk = BSP_I2S_MCLK, .bclk = BSP_I2S_SCLK, .ws = BSP_I2S_LCLK,
                     .dout = BSP_I2S_DOUT, .din = BSP_I2S_DSIN},
    };
    if (i2s_channel_init_std_mode(tx, &bus) != ESP_OK ||
        i2s_channel_init_std_mode(rx, &bus) != ESP_OK) goto fail;
    audio_codec_i2s_cfg_t io = {.port = CONFIG_BSP_I2S_NUM, .rx_handle = rx, .tx_handle = tx};
    data = audio_codec_new_i2s_data(&io);
    audio_codec_i2c_cfg_t control = {.port = BSP_I2C_NUM, .addr = ES8311_CODEC_DEFAULT_ADDR,
                                    .bus_handle = bsp_i2c_get_handle()};
    ctrl = audio_codec_new_i2c_ctrl(&control);
    gpio = audio_codec_new_gpio();
    if (!data || !ctrl || !gpio) goto fail;
    es8311_codec_cfg_t config = {.ctrl_if = ctrl, .gpio_if = gpio,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH, .pa_pin = BSP_POWER_AMP_IO,
        .use_mclk = true, .no_dac_ref = false,
        .hw_gain = {.pa_voltage = 5.0f, .codec_dac_voltage = 3.3f}};
    codec = es8311_codec_new(&config);
    if (!codec) goto fail;
    esp_codec_dev_cfg_t dev = {.dev_type = ESP_CODEC_DEV_TYPE_IN_OUT, .codec_if = codec, .data_if = data};
    s_codec = esp_codec_dev_new(&dev);
    s_mono = heap_caps_malloc(OUT_CAP * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_stereo = heap_caps_malloc(OUT_CAP * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    esp_codec_dev_sample_info_t format = {.sample_rate = HW_RATE, .channel = 2, .bits_per_sample = 16};
    if (!s_codec || !s_mono || !s_stereo || esp_codec_dev_open(s_codec, &format) != ESP_CODEC_DEV_OK ||
        esp_codec_dev_set_in_gain(s_codec, 24.0f) != ESP_CODEC_DEV_OK) goto fail;
    gpio_set_level(BSP_POWER_AMP_IO, 0);
    atomic_store(&s_ready, true);
    return true;
fail:
    if (s_codec) { esp_codec_dev_close(s_codec); esp_codec_dev_delete(s_codec); s_codec = NULL; }
    if (codec) audio_codec_delete_codec_if(codec);
    if (ctrl) audio_codec_delete_ctrl_if(ctrl);
    if (gpio) audio_codec_delete_gpio_if(gpio);
    if (data) audio_codec_delete_data_if(data);
    if (tx) { i2s_channel_disable(tx); i2s_del_channel(tx); }
    if (rx) { i2s_channel_disable(rx); i2s_del_channel(rx); }
    free(s_mono); free(s_stereo); s_mono = s_stereo = NULL;
    return false;
}

void audio_device_voice_priority(bool wanted) { atomic_store(&s_voice_wanted, wanted); }
bool audio_device_voice_waiting(void) { return atomic_load(&s_voice_wanted); }
bool audio_device_acquire(audio_owner_t owner)
{
    if (!atomic_load(&s_ready) || owner == AUDIO_OWNER_NONE ||
        (owner == AUDIO_OWNER_MEDIA && audio_device_voice_waiting())) return false;
    int empty = AUDIO_OWNER_NONE;
    return atomic_compare_exchange_strong(&s_owner, &empty, owner);
}
void audio_device_release(audio_owner_t owner)
{
    if (atomic_load(&s_owner) != owner) return;
    /* Drain the 60 ms DMA queue before disabling the amp. */
    int16_t silence[480 * 2] = {0};
    for (int i = 0; i < 7; ++i) esp_codec_dev_write(s_codec, silence, sizeof(silence));
    gpio_set_level(BSP_POWER_AMP_IO, 0);
    if (s_converter) esp_ae_rate_cvt_close(s_converter);
    s_converter = NULL; s_rate = 0; s_volume = -1;
    atomic_store(&s_owner, AUDIO_OWNER_NONE);
}
bool audio_device_configure(audio_owner_t owner, bool input, unsigned rate, unsigned volume)
{
    if (atomic_load(&s_owner) != owner || input || rate < 8000 || rate > HW_RATE) return false;
    if (s_rate != rate) {
        if (s_converter) esp_ae_rate_cvt_close(s_converter);
        s_converter = NULL; s_rate = 0;
        if (rate != HW_RATE) {
            esp_ae_rate_cvt_cfg_t cfg = {.src_rate = rate, .dest_rate = HW_RATE, .channel = 1,
                .bits_per_sample = 16, .complexity = 2, .perf_type = ESP_AE_RATE_CVT_PERF_TYPE_MEMORY};
            if (esp_ae_rate_cvt_open(&cfg, &s_converter) != ESP_AE_ERR_OK) return false;
        }
        s_rate = rate;
    }
    return audio_device_volume(owner, volume);
}
bool audio_device_volume(audio_owner_t owner, unsigned volume)
{
    if (atomic_load(&s_owner) != owner || !atomic_load(&s_ready) || volume > 100) return false;
    if (s_volume == (int)volume) return true;
    if (esp_codec_dev_set_out_vol(s_codec, volume) != ESP_CODEC_DEV_OK) return false;
    s_volume = volume;
    return true;
}
bool audio_device_capture(int16_t *pcm, size_t frames)
{
    /* One capture task. IN/OUT use independent I2S driver locks. */
    return atomic_load(&s_ready) && pcm && frames <= 4096 &&
           esp_codec_dev_read(s_codec, pcm, frames * 2 * sizeof(int16_t)) == ESP_CODEC_DEV_OK;
}
bool audio_device_write(audio_owner_t owner, const void *pcm, size_t bytes)
{
    if (atomic_load(&s_owner) != owner || !s_rate || !pcm || bytes % 2) return false;
    const int16_t *samples = pcm;
    gpio_set_level(BSP_POWER_AMP_IO, 1);
    for (size_t left = bytes / 2; left;) {
        uint32_t count = left > 240 ? 240 : left, converted = count;
        const int16_t *out = samples;
        if (s_converter) {
            if (esp_ae_rate_cvt_get_max_out_sample_num(s_converter, count, &converted) != ESP_AE_ERR_OK ||
                converted > OUT_CAP) return false;
            converted = OUT_CAP;
            if (esp_ae_rate_cvt_process(s_converter, (void *)samples, count, s_mono, &converted) != ESP_AE_ERR_OK)
                return false;
            out = s_mono;
        }
        for (unsigned i = 0; i < converted; ++i) s_stereo[2*i] = s_stereo[2*i+1] = out[i];
        if (converted && esp_codec_dev_write(s_codec, s_stereo, converted * 4) != ESP_CODEC_DEV_OK) return false;
        samples += count; left -= count;
    }
    return true;
}
