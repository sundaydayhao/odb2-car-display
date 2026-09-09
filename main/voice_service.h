#pragma once
#include <stdbool.h>
#include "esp_err.h"

esp_err_t voice_service_start(void);
void voice_set_pressed(bool pressed);
bool voice_is_active(void);
