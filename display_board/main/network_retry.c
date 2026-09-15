#include "network_retry.h"

static void wait_after_failure(network_retry_t *p, int64_t now_ms)
{
    p->phase = NET_RETRY_WAIT;
    p->due_ms = now_ms + NET_RETRY_BACKOFF_MS;
}

static network_retry_action_t stop(network_retry_t *p)
{
    p->phase = NET_RETRY_FAILED;
    return NET_RETRY_STOP;
}

void network_retry_reset(network_retry_t *p, int64_t now_ms)
{
    *p = (network_retry_t) {
        .phase = NET_RETRY_WAIT,
        .due_ms = now_ms,
        .expires_ms = now_ms + NET_RETRY_BUDGET_MS,
    };
}

void network_retry_associated(network_retry_t *p, int64_t now_ms)
{
    if (p->phase != NET_RETRY_ASSOCIATING) return;
    p->phase = NET_RETRY_DHCP;
    p->due_ms = now_ms + NET_RETRY_DHCP_MS;
}

void network_retry_online(network_retry_t *p)
{
    if (p->phase == NET_RETRY_ASSOCIATING || p->phase == NET_RETRY_DHCP)
        p->phase = NET_RETRY_ONLINE;
}

void network_retry_disconnected(network_retry_t *p, int64_t now_ms)
{
    if (p->phase == NET_RETRY_ONLINE) {
        network_retry_reset(p, now_ms);
        wait_after_failure(p, now_ms);
    } else if (p->phase == NET_RETRY_ASSOCIATING || p->phase == NET_RETRY_DHCP) {
        wait_after_failure(p, now_ms);
    }
}

network_retry_action_t network_retry_step(network_retry_t *p, int64_t now_ms)
{
    if (p->phase == NET_RETRY_ONLINE || p->phase == NET_RETRY_FAILED)
        return NET_RETRY_NONE;
    if (now_ms >= p->expires_ms) return stop(p);

    switch (p->phase) {
    case NET_RETRY_WAIT:
        if (p->attempts >= NET_RETRY_MAX_ATTEMPTS) return stop(p);
        if (now_ms < p->due_ms) return NET_RETRY_NONE;
        ++p->attempts;
        p->phase = NET_RETRY_ASSOCIATING;
        p->due_ms = now_ms + NET_RETRY_ASSOCIATION_MS;
        return NET_RETRY_CONNECT;

    case NET_RETRY_ASSOCIATING:
        if (now_ms < p->due_ms) return NET_RETRY_NONE;
        wait_after_failure(p, now_ms);
        return NET_RETRY_DISCONNECT;

    case NET_RETRY_DHCP:
        if (now_ms < p->due_ms) return NET_RETRY_NONE;
        if (p->radio_restarts) return stop(p);
        if (!p->dhcp_renewals) {
            ++p->dhcp_renewals;
            p->due_ms = now_ms + NET_RETRY_DHCP_MS;
            return NET_RETRY_RENEW_DHCP;
        }
        /* Restarting the radio is useful only if another connect is allowed. */
        if (p->attempts >= NET_RETRY_MAX_ATTEMPTS) return stop(p);
        ++p->radio_restarts;
        wait_after_failure(p, now_ms);
        return NET_RETRY_RESTART_RADIO;

    case NET_RETRY_ONLINE:
    case NET_RETRY_FAILED:
        return NET_RETRY_NONE;
    }
    return NET_RETRY_NONE;
}
