#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "wifi_networks.h"

int main(void)
{
    wifi_networks_t list = {0};
    uint8_t empty[33] = {0};
    wifi_networks_add(&list, empty, -10, NETWORK_OPEN);
    assert(list.count == 0);
    uint8_t name[33] = "Phone";
    wifi_networks_add(&list, name, -70, NETWORK_WPA2);
    wifi_networks_add(&list, name, -80, NETWORK_WPA2);
    assert(list.count == 1 && list.items[0].rssi == -70);
    wifi_networks_add(&list, name, -40, NETWORK_WPA2);
    wifi_networks_add(&list, name, -30, NETWORK_OPEN);
    assert(list.count == 2 && list.items[0].security == NETWORK_OPEN && list.items[1].rssi == -40);
    for (unsigned i = 0; i < 30; ++i) {
        snprintf((char *)name, sizeof(name), "AP-%u", i);
        wifi_networks_add(&list, name, -50 - i, NETWORK_WPA3);
    }
    assert(list.count == WIFI_NETWORK_LIMIT);
    for (unsigned i = 1; i < list.count; ++i) assert(list.items[i-1].rssi >= list.items[i].rssi);
    snprintf((char *)name, sizeof(name), "AP-29");
    wifi_networks_add(&list, name, -20, NETWORK_WPA3);
    assert(list.count == WIFI_NETWORK_LIMIT && !strcmp(list.items[0].ssid, "AP-29"));
    memset(name, 'X', sizeof(name)); /* No terminator in a full driver SSID. */
    wifi_networks_add(&list, name, -10, NETWORK_WPA2);
    assert(strlen(list.items[0].ssid) == 32 && list.items[0].ssid[32] == 0);
    puts("Wi-Fi network ordering, security separation, capacity and SSID bounds passed.");
}
