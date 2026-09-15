#pragma once
#include "dashboard.h"
#include "media_model.h"
#include "av_model.h"

void media_panel_handlers(media_action_cb_t action);
void media_panel_create(lv_obj_t *parent, const dashboard_palette_t *palette);
void media_panel_detach(void);
void media_panel_update(const media_status_t *status);
void media_panel_av_handlers(av_action_cb_t action,av_files_cb_t files);
void media_panel_av_update(const av_status_t *status);
void media_panel_select(av_tab_t tab);
void media_panel_frame(const uint16_t *pixels,bool photo);
void media_panel_camera_clear(void);
