#pragma once
#include <stdbool.h>
#include "lvgl.h"
#include "wifi_networks.h"
#include "media_model.h"

typedef enum { PANEL_VOICE_DOWN, PANEL_VOICE_UP, PANEL_CAMERA_ON, PANEL_CAMERA_OFF,
    PANEL_SEAL, PANEL_UPLOAD_ON, PANEL_UPLOAD_OFF, PANEL_UPLOAD_NOW, PANEL_OTA_CHECK, PANEL_OTA_INSTALL,
    PANEL_WIFI_SAVE, PANEL_PREFERENCES_SAVE, PANEL_BLUETOOTH_ON, PANEL_BLUETOOTH_OFF,
    PANEL_BLUETOOTH_RECONNECT, PANEL_WIFI_SCAN, PANEL_ACTION_COUNT } panel_action_t;
/* Versioned byte-only NVS format. Demo mode is transient. */
typedef struct { uint8_t version, theme, pressure, brightness, daylight, turbo; } panel_preferences_t;
#define PANEL_PREFERENCES_DEFAULT {1, 0, 0, 100, 1, 1}
typedef struct {
    panel_action_t action;
    union {
        struct { char ssid[33], password[65]; } wifi;
        panel_preferences_t preferences;
    } data;
} panel_request_t;
/* Borrowed request: copy before returning; true means accepted, not yet completed. */
typedef bool (*panel_action_cb_t)(const panel_request_t *request);
void service_panel_create(lv_obj_t *screen, lv_obj_t *toolbar, panel_action_cb_t action);
void service_panel_open(void);
void service_panel_open_media(void);
void service_panel_media_handler(media_action_cb_t action);
void service_panel_media_config(const char *music, const char *radio, const media_status_t *status);
void service_panel_status(unsigned id, unsigned state, const char *detail);
void service_panel_upload_enabled(bool enabled);
void service_panel_network_config(const char *ssid, const char *password);
void service_panel_network_scan(const wifi_networks_t *networks);
void service_panel_bluetooth(bool available, bool enabled, unsigned state, const char *detail, const char *address);
void service_panel_preferences(const panel_preferences_t *preferences);
bool service_panel_preferences_valid(const panel_preferences_t *preferences);
void service_panel_save_result(bool success);
void service_panel_camera_visible(bool visible);
void service_panel_camera_frame(const uint16_t *pixels);
void service_panel_camera_clear(void);
