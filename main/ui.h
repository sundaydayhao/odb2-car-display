#pragma once

#include "obd2.h"

bool ui_init(void);
bool ui_healthy(void);
bool ui_open_media(void);
void ui_camera_frame(const uint16_t *pixels);
void ui_camera_clear(void);
void ui_render_vehicle_data(const obd2_vehicle_data_t *data);
