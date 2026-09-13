#include "node_power.h"
#include <string.h>
bool node_power_config_valid(const node_power_config_t *c) {
    return c->idle_s >= 30 && c->idle_s <= 600 && c->wake_s >= 60 && c->wake_s <= 3600 &&
        c->cutoff_mv >= 11000 && c->cutoff_mv <= 12500;
}
void node_power_init(node_power_t *p, const node_power_config_t *c, uint64_t now, uint32_t window_ms) {
    memset(p, 0, sizeof(*p)); p->config = *c; p->deadline = now + window_ms;
}
void node_power_touch(node_power_t *p, uint64_t now) { p->deadline = now + p->config.idle_s * 1000ULL; }
node_sleep_reason_t node_power_check(node_power_t *p, uint64_t now, int mv, bool usb) {
    /* Connected USB bench power bypasses automatic sleep, not the CAN transmit whitelist. */
    if (p->manual) return NODE_SLEEP_MANUAL;
    if (usb) { p->low_tracking = false; return NODE_AWAKE; }
    if (mv <= (int)p->config.cutoff_mv) {
        if (!p->low_tracking) { p->low_tracking = true; p->low_since = now; }
        if (now - p->low_since >= 30000) return NODE_SLEEP_LOW_BATTERY;
    } else if (mv > (int)p->config.cutoff_mv + 100) p->low_tracking = false;
    /* Bus chatter and a connected-but-idle phone never extend this deadline. */
    return now >= p->deadline ? NODE_SLEEP_IDLE : NODE_AWAKE;
}
