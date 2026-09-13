#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef void (*node_ble_rx_fn)(const uint8_t *, size_t, uint32_t generation);
esp_err_t node_ble_init(node_ble_rx_fn);
bool node_ble_send(const char *, size_t);
bool node_ble_connected(void);
uint32_t node_ble_generation(void);
uint32_t node_ble_dropped(void);
void node_ble_stop(void);
void node_ble_disconnect(void);
