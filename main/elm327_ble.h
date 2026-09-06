#pragma once

#include "esp_err.h"
#include "elm327_protocol.h"

typedef enum { ELM_BLE_SCANNING, ELM_BLE_CONNECTING, ELM_BLE_DISCOVERING, ELM_BLE_READY } elm_ble_state_t;
typedef struct { elm_ble_state_t state; uint32_t generation; } elm_ble_status_t;

esp_err_t elm_ble_init(void);
elm_ble_status_t elm_ble_status(void);
void elm_ble_reset_link(void);
/* One command in flight. A missing prompt/overflow invalidates the entire link. */
esp_err_t elm_ble_command(const char *command, elm_response_t *response, uint32_t timeout_ms);
