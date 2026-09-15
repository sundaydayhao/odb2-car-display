#pragma once
#include <stdbool.h>

/* Pair a successful begin with end immediately around esp_opus_enc_open(),
 * including its failure path. Scopes cannot be nested or shared by tasks.
 * A failed begin does not change the existing scope; do not end that scope. */
bool voice_codec_memory_begin(void);
void voice_codec_memory_end(void);
/* MP3 decoder state uses PSRAM, preserving internal RAM for radio/DMA/voice. */
bool media_codec_memory_begin(void);
void media_codec_memory_end(void);
