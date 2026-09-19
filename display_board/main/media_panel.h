#pragma once
#include "dashboard.h"
#include "media_model.h"
#include "av_model.h"
#include "fm_model.h"

void media_panel_handlers(media_action_cb_t action);
void media_panel_create(lv_obj_t *parent, const dashboard_palette_t *palette);
void media_panel_detach(void);
void media_panel_update(const media_status_t *status);
void media_panel_av_handlers(av_action_cb_t action,av_files_cb_t files);
void media_panel_av_update(const av_status_t *status);
void media_panel_select(av_tab_t tab);
void media_panel_frame(const uint16_t *pixels,bool photo);
void media_panel_camera_clear(void);
void media_panel_fm_handler(fm_presets_save_cb_t save);
bool media_panel_fm_restore(const fm_presets_t *presets);
void media_panel_fm_save_result(bool success);
void media_panel_fm_control(fm_action_cb_t action);
void media_panel_show_camera(void);
void media_panel_fm_update(const fm_status_t *status);
