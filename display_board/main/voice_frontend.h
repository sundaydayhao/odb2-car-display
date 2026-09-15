#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "voice_strings.h"

#define VOICE_FRAME_SAMPLES 512
typedef struct {
    uint32_t generation;
    int64_t wake_at_us;
    uint16_t samples;
    bool wake, speech, preroll, wake_end;
    int16_t pcm[VOICE_FRAME_SAMPLES];
} voice_frame_t;
esp_err_t voice_frontend_start(void);
void voice_frontend_set_enabled(bool enabled);
/* Changes invalidate all previous frames; only the voice service calls these. */
void voice_frontend_arm(void);
void voice_frontend_pause(void);
/* Start fresh question capture after reply playback.
 * Unlike arm(), normal speech is accepted without another wake word. */
void voice_frontend_listen(void);
/* A wake event has no PCM. It is followed by approximately two seconds of
 * chronological preroll PCM, whose last frame has wake_end set, then live PCM.
 * All of these frames belong to the same generation. */
bool voice_frontend_receive(voice_frame_t *frame, TickType_t wait);
bool voice_frontend_failed(void);
