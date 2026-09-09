#pragma once
#include <stdbool.h>
#include "esp_err.h"

#define CAMERA_PREVIEW_WIDTH 320
#define CAMERA_PREVIEW_HEIGHT 240
esp_err_t camera_service_start(void);
void camera_set_enabled(bool enabled);
bool camera_is_enabled(void);
