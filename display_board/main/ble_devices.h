#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { BLE_DEVICE_LIMIT = 16, BLE_DEVICE_NAME_SIZE = 40 };
typedef struct {
    uint8_t type, address[6];
    char name[BLE_DEVICE_NAME_SIZE];
    int8_t rssi;
} ble_device_t;
typedef struct {
    uint32_t revision;
    unsigned count;
    bool scanning, has_saved;
    ble_device_t items[BLE_DEVICE_LIMIT], saved;
    char message[96];
} ble_devices_t;

bool ble_device_same(const ble_device_t *a, const ble_device_t *b);
void ble_device_address(const ble_device_t *device, char address[18]);
/* Stable insertion order: repeated advertisements cannot move a tapped row. */
void ble_devices_add(ble_devices_t *list, const ble_device_t *device);

typedef struct { bool startup_consumed, allowed; } ble_reconnect_policy_t;
bool ble_reconnect_startup(ble_reconnect_policy_t *policy, bool enabled, bool saved);
bool ble_reconnect_take(ble_reconnect_policy_t *policy, bool enabled, bool saved, bool manual);
