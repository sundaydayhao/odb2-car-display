#include "wifi_networks.h"
#include <string.h>

void wifi_networks_add(wifi_networks_t *list, const uint8_t ssid[33], int8_t rssi, network_security_t security)
{
    if (!list || !ssid || !ssid[0]) return;
    wifi_network_t item = {.rssi = rssi, .security = security};
    memcpy(item.ssid, ssid, 32); /* Driver SSIDs can occupy all 32 bytes. */
    for (unsigned i = 0; i < list->count; ++i) {
        if (!strcmp(list->items[i].ssid, item.ssid) && list->items[i].security == security) {
            if (list->items[i].rssi >= rssi) return;
            for (unsigned j = i; j + 1 < list->count; ++j) list->items[j] = list->items[j + 1];
            --list->count;
            break;
        }
    }
    unsigned position = 0;
    while (position < list->count && list->items[position].rssi >= rssi) ++position;
    if (position >= WIFI_NETWORK_LIMIT) return;
    if (list->count < WIFI_NETWORK_LIMIT) ++list->count;
    for (unsigned i = list->count - 1; i > position; --i) list->items[i] = list->items[i - 1];
    list->items[position] = item;
}
