#pragma once
#include <stdbool.h>
#include "lvgl.h"

typedef enum { PANEL_VOICE_DOWN, PANEL_VOICE_UP, PANEL_CAMERA_ON, PANEL_CAMERA_OFF,
    PANEL_SEAL, PANEL_UPLOAD_ON, PANEL_UPLOAD_OFF, PANEL_UPLOAD_NOW, PANEL_OTA_CHECK, PANEL_OTA_INSTALL } panel_action_t;
typedef void (*panel_action_cb_t)(panel_action_t action);
void service_panel_create(lv_obj_t *screen, lv_obj_t *toolbar, panel_action_cb_t action);
void service_panel_status(unsigned id, unsigned state, const char *detail);
void service_panel_upload_enabled(bool enabled);
void service_panel_camera_visible(bool visible);
void service_panel_camera_frame(const uint16_t *pixels);
void service_panel_camera_clear(void);
