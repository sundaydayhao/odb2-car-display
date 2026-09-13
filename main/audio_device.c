#include "audio_device.h"
#include <stdatomic.h>
#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"

static atomic_int s_owner;
static atomic_bool s_voice_wanted;
static esp_codec_dev_handle_t s_speaker, s_mic, s_open;
static unsigned s_rate;
static int s_volume = -1;

void audio_device_voice_priority(bool wanted) { atomic_store(&s_voice_wanted, wanted); }
bool audio_device_voice_waiting(void) { return atomic_load(&s_voice_wanted); }
bool audio_device_acquire(audio_owner_t owner)
{
    if (owner == AUDIO_OWNER_NONE || (owner == AUDIO_OWNER_MEDIA && audio_device_voice_waiting())) return false;
    int empty = AUDIO_OWNER_NONE;
    return atomic_compare_exchange_strong(&s_owner, &empty, owner);
}
void audio_device_release(audio_owner_t owner)
{
    if (atomic_load(&s_owner) != owner) return;
    if (s_open) esp_codec_dev_close(s_open);
    s_open = NULL;
    s_rate = 0;
    s_volume = -1;
    atomic_store(&s_owner, AUDIO_OWNER_NONE);
}
bool audio_device_configure(audio_owner_t owner, bool input, unsigned rate, unsigned volume)
{
    if (atomic_load(&s_owner) != owner || rate < 8000 || rate > 48000) return false;
    if (!s_speaker) s_speaker = bsp_audio_codec_speaker_init();
    if (input && !s_mic) s_mic = bsp_audio_codec_microphone_init();
    esp_codec_dev_handle_t target = input ? s_mic : s_speaker;
    if (!target) return false;
    if (s_open != target || s_rate != rate) {
        if (s_open) esp_codec_dev_close(s_open);
        s_open = NULL; s_rate = 0; s_volume = -1;
        esp_codec_dev_sample_info_t format = {.sample_rate = rate, .channel = 1, .bits_per_sample = 16};
        if (esp_codec_dev_open(target, &format) != ESP_CODEC_DEV_OK) return false;
        s_open = target; s_rate = rate;
    }
    return input ? esp_codec_dev_set_in_gain(target, 24.0f) == ESP_CODEC_DEV_OK : audio_device_volume(owner, volume);
}
bool audio_device_volume(audio_owner_t owner, unsigned volume)
{
    if (atomic_load(&s_owner) != owner || !s_speaker || volume > 100) return false;
    if (s_volume == (int)volume) return true;
    if (esp_codec_dev_set_out_vol(s_speaker, volume) != ESP_CODEC_DEV_OK) return false;
    s_volume = volume;
    return true;
}
bool audio_device_read(audio_owner_t owner, void *pcm, size_t bytes)
{
    return atomic_load(&s_owner) == owner && s_open == s_mic && s_open &&
           esp_codec_dev_read(s_open, pcm, (int)bytes) == ESP_CODEC_DEV_OK;
}
bool audio_device_write(audio_owner_t owner, const void *pcm, size_t bytes)
{
    return atomic_load(&s_owner) == owner && s_open == s_speaker && s_open &&
           esp_codec_dev_write(s_open, (void *)pcm, (int)bytes) == ESP_CODEC_DEV_OK;
}
