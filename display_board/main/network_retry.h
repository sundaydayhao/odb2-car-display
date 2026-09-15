#pragma once

#include <stdint.h>

typedef enum {
    NET_RETRY_WAIT,
    NET_RETRY_ASSOCIATING,
    NET_RETRY_DHCP,
    NET_RETRY_ONLINE,
    NET_RETRY_FAILED,
} network_retry_phase_t;

typedef enum {
    NET_RETRY_NONE,
    NET_RETRY_CONNECT,
    NET_RETRY_DISCONNECT,
    NET_RETRY_RENEW_DHCP,
    NET_RETRY_RESTART_RADIO,
    NET_RETRY_STOP,
} network_retry_action_t;

enum {
    NET_RETRY_BUDGET_MS = 90000,
    NET_RETRY_ASSOCIATION_MS = 20000,
    NET_RETRY_DHCP_MS = 15000,
    NET_RETRY_BACKOFF_MS = 2000,
    NET_RETRY_MAX_ATTEMPTS = 3,
};

/* Owned by the network task. All times use the same monotonic millisecond
 * clock. A reset starts one bounded recovery episode; the deadline and the
 * recovery counters survive failed connections within that episode. */
typedef struct {
    network_retry_phase_t phase;
    unsigned attempts;
    unsigned dhcp_renewals;
    unsigned radio_restarts;
    int64_t due_ms;     /* Current phase deadline / earliest next connection. */
    int64_t expires_ms; /* Entire recovery episode deadline. */
} network_retry_t;

void network_retry_reset(network_retry_t *p, int64_t now_ms);
void network_retry_associated(network_retry_t *p, int64_t now_ms);
void network_retry_online(network_retry_t *p);
void network_retry_disconnected(network_retry_t *p, int64_t now_ms);

/* Call after processing actual network events. Duplicate association and
 * disconnect events do not extend deadlines. LOST_IP alone is not a
 * disconnect: the caller must check the current netif and association.
 *
 * Actions describe work for the caller. DISCONNECT and RESTART_RADIO already
 * enter WAIT; the caller must finish the requested stop/start operation before
 * allowing another CONNECT. STOP also requires caller-side disconnection.
 * FAILED is terminal until reset, and STOP is emitted only once. */
network_retry_action_t network_retry_step(network_retry_t *p, int64_t now_ms);
