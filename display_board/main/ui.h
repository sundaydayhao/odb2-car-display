#pragma once
#include "av_model.h"

#include "obd2.h"

bool ui_init(void);
bool ui_healthy(void);
bool ui_open_media(void);
bool ui_open_av(av_tab_t tab);
void ui_camera_frame(const uint16_t *pixels);
void ui_camera_clear(void);
void ui_media_photo_frame(const uint16_t *pixels);
void ui_render_vehicle_data(const obd2_vehicle_data_t *data);
