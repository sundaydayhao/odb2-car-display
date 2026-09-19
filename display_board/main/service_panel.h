#pragma once
#include "fm_model.h"
#include "alarm_model.h"
#include <stdbool.h>
#include "lvgl.h"
#include "wifi_networks.h"
#include "ble_devices.h"
#include "media_model.h"

typedef enum { PANEL_CAMERA_ON, PANEL_CAMERA_OFF,
    PANEL_SEAL, PANEL_UPLOAD_ON, PANEL_UPLOAD_OFF, PANEL_UPLOAD_NOW, PANEL_OTA_CHECK, PANEL_OTA_INSTALL,
    PANEL_WIFI_SAVE, PANEL_PREFERENCES_SAVE, PANEL_BLUETOOTH_ON, PANEL_BLUETOOTH_OFF,
    PANEL_BLUETOOTH_RECONNECT, PANEL_WIFI_SCAN, PANEL_BLUETOOTH_SCAN,
    PANEL_BLUETOOTH_CONNECT, PANEL_BLUETOOTH_DISCONNECT, PANEL_BLUETOOTH_FORGET,
    PANEL_VOICE_ON, PANEL_VOICE_OFF, PANEL_VOICE_START, PANEL_VOICE_END, PANEL_VOLUME, PANEL_FM_PRESETS_SAVE,
    PANEL_ALARM_ADD, PANEL_ALARM_DELETE, PANEL_ALARM_DISMISS,
    PANEL_ACTION_COUNT } panel_action_t;
/* Versioned byte-only NVS format. Demo mode is transient. */
typedef struct { uint8_t version, theme, pressure, brightness, daylight, turbo; } panel_preferences_t;
#define PANEL_PREFERENCES_DEFAULT {1, 0, 0, 100, 1, 1}
typedef struct {
    panel_action_t action;
    union {
        struct { char ssid[33], password[65]; } wifi;
        panel_preferences_t preferences;
        ble_device_t bluetooth;
        uint8_t volume;
        fm_presets_t fm_presets;
        struct { unsigned hour,minute; bool daily; uint32_t id; } alarm;
    } data;
} panel_request_t;
/* Borrowed request: copy before returning; true means accepted, not yet completed. */
typedef bool (*panel_action_cb_t)(const panel_request_t *request);
void service_panel_create(lv_obj_t *screen, lv_obj_t *toolbar, panel_action_cb_t action);
void service_panel_open(void);
void service_panel_open_voice(void);
void service_panel_close(void);
void service_panel_open_alarms(void);
void service_panel_alarms(const alarm_status_t *status);
void service_panel_media(const media_status_t *status);
void service_panel_fm(const fm_status_t *status);
void service_panel_status(unsigned id, unsigned state, const char *detail);
void service_panel_voice(bool enabled, bool active);
void service_panel_upload_enabled(bool enabled);
void service_panel_network_config(const char *ssid, const char *password);
void service_panel_network_scan(const wifi_networks_t *networks);
void service_panel_bluetooth(bool available, bool enabled, unsigned state, const char *detail, const char *address);
void service_panel_gps(bool available, unsigned state, const char *detail);
void service_panel_bluetooth_devices(const ble_devices_t *devices);
void service_panel_preferences(const panel_preferences_t *preferences);
bool service_panel_preferences_valid(const panel_preferences_t *preferences);
void service_panel_save_result(bool success);
void service_panel_camera_visible(bool visible);
void service_panel_camera_frame(const uint16_t *pixels);
void service_panel_camera_clear(void);
