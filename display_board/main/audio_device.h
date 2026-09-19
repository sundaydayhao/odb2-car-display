#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { AUDIO_OWNER_NONE, AUDIO_OWNER_VOICE, AUDIO_OWNER_MEDIA, AUDIO_OWNER_RADIO, AUDIO_OWNER_ALARM } audio_owner_t;
/* One task continuously captures; one output owner plays at a time. */
bool audio_device_init(void);
bool audio_device_acquire(audio_owner_t owner);
void audio_device_release(audio_owner_t owner);
void audio_device_voice_priority(bool wanted);
bool audio_device_voice_waiting(void);
/* Analog FM reserves output ownership for source arbitration; it does not use I2S. */
void audio_device_radio_priority(bool wanted);
bool audio_device_radio_waiting(void);
bool audio_device_radio_owned(void);
void audio_device_alarm_priority(bool wanted);
bool audio_device_alarm_waiting(void);
bool audio_device_configure(audio_owner_t owner, bool input, unsigned rate);
/* Thread-safe control; only the active output task touches codec registers. */
void audio_device_set_volume(unsigned volume);
unsigned audio_device_get_volume(void);
/* 48 kHz stereo: microphone left, ES8311 DAC reference right. */
bool audio_device_capture(int16_t *pcm, size_t frames);
bool audio_device_write(audio_owner_t owner, const void *pcm, size_t bytes);
