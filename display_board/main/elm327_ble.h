#pragma once

#include "esp_err.h"
#include "elm327_protocol.h"
#include "ble_devices.h"

typedef enum { ELM_BLE_SCANNING, ELM_BLE_CONNECTING, ELM_BLE_DISCOVERING, ELM_BLE_READY,
    ELM_BLE_OFF, ELM_BLE_STOPPING, ELM_BLE_IDLE } elm_ble_state_t;
typedef struct {
    elm_ble_state_t state;
    uint32_t generation;
    bool available, enabled;
    char address[18];
} elm_ble_status_t;

esp_err_t elm_ble_init(void);
elm_ble_status_t elm_ble_status(void);
void elm_ble_reset_link(void);
/* Persist on a worker task; host watchdog applies requests without stopping shared Wi-Fi. */
esp_err_t elm_ble_set_enabled(bool enabled);
/* Scan only; selecting a result is a separate, bounded connection request. */
esp_err_t elm_ble_scan(void);
void elm_ble_devices_get(ble_devices_t *devices);
esp_err_t elm_ble_connect_device(const ble_device_t *device);
esp_err_t elm_ble_disconnect(void);
esp_err_t elm_ble_forget(void);
/* One attempt to the remembered device. No fallback to another device. */
esp_err_t elm_ble_reconnect(void);
/* Called by the OBD worker only after ELM identity and required AT commands pass. */
esp_err_t elm_ble_confirm_adapter(uint32_t generation);
/* One command in flight. A missing prompt/overflow invalidates the entire link. */
esp_err_t elm_ble_command(const char *command, elm_response_t *response, uint32_t timeout_ms);
