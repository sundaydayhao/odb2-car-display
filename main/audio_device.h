#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { AUDIO_OWNER_NONE, AUDIO_OWNER_VOICE, AUDIO_OWNER_MEDIA } audio_owner_t;
/* One session owns the shared ES8311/I2S pair. Only its task performs I/O. */
bool audio_device_acquire(audio_owner_t owner);
void audio_device_release(audio_owner_t owner);
void audio_device_voice_priority(bool wanted);
bool audio_device_voice_waiting(void);
bool audio_device_configure(audio_owner_t owner, bool input, unsigned rate, unsigned volume);
bool audio_device_volume(audio_owner_t owner, unsigned volume);
bool audio_device_read(audio_owner_t owner, void *pcm, size_t bytes);
bool audio_device_write(audio_owner_t owner, const void *pcm, size_t bytes);
