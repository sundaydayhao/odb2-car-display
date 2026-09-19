#pragma once
#include "av_model.h"

#include "obd2.h"
struct map_panel_stats;

bool ui_init(void);
bool ui_healthy(void);
bool ui_open_media(void);
bool ui_open_av(av_tab_t tab);
bool ui_open_vehicle_page(unsigned page);
bool ui_open_alarms(void);
bool ui_open_map(void);
bool ui_map_status(struct map_panel_stats *out);
void ui_camera_frame(const uint16_t *pixels);
void ui_camera_clear(void);
void ui_media_photo_frame(const uint16_t *pixels);
void ui_render_vehicle_data(const obd2_vehicle_data_t *data);
