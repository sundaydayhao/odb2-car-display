#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "wifi_networks.h"

#define DEVICE_BOARD_ID "esp32-p4-function-ev-board-v1.4"
#define DEVICE_PROJECT_ID "odb2_dash"

typedef enum { SERVICE_OFF, SERVICE_WAITING, SERVICE_BUSY, SERVICE_READY, SERVICE_ERROR } service_state_t;
typedef enum { SERVICE_NETWORK, SERVICE_VOICE, SERVICE_CAMERA, SERVICE_RECORD, SERVICE_UPLOAD, SERVICE_OTA, SERVICE_COUNT } service_id_t;
typedef struct {
    service_state_t state[SERVICE_COUNT];
    char detail[SERVICE_COUNT][96];
} device_status_t;
typedef struct {
    char ssid[33], password[65], upload_url[256], ota_url[256], token[129];
    bool upload_enabled;
} device_settings_t;

esp_err_t device_services_init(void);
void device_status_set(service_id_t id, service_state_t state, const char *detail);
void device_status_get(device_status_t *out);
/* Nonblocking snapshots for the display task; false leaves both outputs untouched. */
bool device_ui_snapshot(device_status_t *status, device_settings_t *settings);
void device_settings_get(device_settings_t *out);
esp_err_t device_settings_save(const device_settings_t *settings);
bool device_url_valid(const char *url);
bool device_network_ready(void);
bool device_clock_ready(void);
esp_err_t device_network_start(void);
void device_network_reconfigure(void);
esp_err_t device_network_scan_request(void);
void device_network_scan_get(wifi_networks_t *out);
const char *device_id(void);
const char *device_boot_id(void);
bool device_maintenance_allowed(bool bench);
esp_err_t device_console_start(void);
