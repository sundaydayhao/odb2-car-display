#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct { uint32_t idle_s, wake_s, cutoff_mv; } node_power_config_t;
typedef enum { NODE_AWAKE, NODE_SLEEP_IDLE, NODE_SLEEP_LOW_BATTERY, NODE_SLEEP_MANUAL } node_sleep_reason_t;
typedef struct {
    node_power_config_t config;
    uint64_t deadline, low_since;
    bool low_tracking, manual;
} node_power_t;
void node_power_init(node_power_t *, const node_power_config_t *, uint64_t now, uint32_t window_ms);
void node_power_touch(node_power_t *, uint64_t now);
node_sleep_reason_t node_power_check(node_power_t *, uint64_t now, int protected_mv, bool usb_powered);
bool node_power_config_valid(const node_power_config_t *);
