#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t voice_service_start(void);
bool voice_is_active(void);
bool voice_is_enabled(void);
void voice_set_enabled(bool enabled);
bool voice_begin_conversation(void);
void voice_end_conversation(void);
/* ESP-IDF reports the task stack high-water mark in bytes. */
uint32_t voice_stack_min_free(void);
void voice_log_diagnostics(void);
