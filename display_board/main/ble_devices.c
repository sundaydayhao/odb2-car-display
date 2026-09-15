#include "ble_devices.h"
#include <stdio.h>
#include <string.h>

bool ble_device_same(const ble_device_t *a, const ble_device_t *b)
{
    return a && b && a->type == b->type && !memcmp(a->address, b->address, 6);
}
void ble_device_address(const ble_device_t *device, char address[18])
{
    const uint8_t *a = device->address;
    snprintf(address, 18, "%02X:%02X:%02X:%02X:%02X:%02X", a[5], a[4], a[3], a[2], a[1], a[0]);
}
void ble_devices_add(ble_devices_t *list, const ble_device_t *device)
{
    if (!list || !device || device->type > 3) return;
    ble_device_t bounded=*device;
    bounded.name[BLE_DEVICE_NAME_SIZE-1]=0;
    device=&bounded;
    unsigned i = 0;
    while (i < list->count && !ble_device_same(&list->items[i], device)) ++i;
    if (i >= BLE_DEVICE_LIMIT) return;
    if (i == list->count) {
        list->items[i] = *device;
        list->items[i].name[BLE_DEVICE_NAME_SIZE - 1] = 0;
        ++list->count;
        ++list->revision;
    } else if (*device->name && strcmp(list->items[i].name, device->name)) {
        memcpy(list->items[i].name,device->name,sizeof(list->items[i].name));
        ++list->revision;
    }
    /* RSSI fluctuations do not continuously rebuild the touch targets. */
    list->items[i].rssi = device->rssi;
}

bool ble_reconnect_startup(ble_reconnect_policy_t *policy, bool enabled, bool saved)
{
    bool attempt=!policy->startup_consumed && enabled && saved;
    policy->startup_consumed=true;
    return attempt;
}
bool ble_reconnect_take(ble_reconnect_policy_t *policy, bool enabled, bool saved, bool manual)
{
    bool attempt=policy->allowed && enabled && saved && !manual;
    policy->allowed=false;
    return attempt;
}
