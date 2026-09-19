#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "av_model.h"

#define CAMERA_PREVIEW_WIDTH AV_PREVIEW_WIDTH
#define CAMERA_PREVIEW_HEIGHT AV_PREVIEW_HEIGHT
esp_err_t camera_service_start(void);
void camera_set_enabled(bool enabled);
bool camera_is_enabled(void);
bool camera_take_photo(void);
/* 0 idle, 1 queued, 2 saving, 3 saved, 4 failed/cancelled. */
unsigned camera_photo_state(void);
