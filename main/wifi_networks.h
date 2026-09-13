#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { WIFI_NETWORK_LIMIT = 20 };
typedef enum { NETWORK_OPEN, NETWORK_WPA2, NETWORK_WPA3, NETWORK_UNSUPPORTED } network_security_t;
typedef enum { NETWORK_SCAN_IDLE, NETWORK_SCAN_BUSY, NETWORK_SCAN_READY, NETWORK_SCAN_ERROR } network_scan_state_t;
typedef struct {
    char ssid[33];
    int8_t rssi;
    network_security_t security;
} wifi_network_t;
typedef struct {
    uint32_t revision;
    network_scan_state_t state;
    unsigned count;
    wifi_network_t items[WIFI_NETWORK_LIMIT];
    char message[96], connected_ssid[33];
    network_security_t connected_security;
    bool connected;
} wifi_networks_t;

/* Retain the strongest named networks, without merging different security modes. */
void wifi_networks_add(wifi_networks_t *list, const uint8_t ssid[33], int8_t rssi, network_security_t security);
