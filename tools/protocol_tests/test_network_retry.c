#include <assert.h>
#include <stdio.h>
#include "network_retry.h"

static void test_success_and_new_disconnect_budget(void)
{
    network_retry_t p;
    network_retry_reset(&p, 1000);
    assert(p.phase == NET_RETRY_WAIT && p.expires_ms == 91000);
    assert(network_retry_step(&p, 1000) == NET_RETRY_CONNECT);
    assert(p.attempts == 1 && p.due_ms == 21000);
    network_retry_associated(&p, 1800);
    assert(p.phase == NET_RETRY_DHCP && p.due_ms == 16800);
    network_retry_online(&p);
    assert(network_retry_step(&p, 999999) == NET_RETRY_NONE);
    assert(p.phase == NET_RETRY_ONLINE);

    network_retry_disconnected(&p, 1000000);
    assert(p.phase == NET_RETRY_WAIT && p.attempts == 0);
    assert(p.expires_ms == 1090000 && p.due_ms == 1002000);
    assert(p.dhcp_renewals == 0 && p.radio_restarts == 0);
    network_retry_disconnected(&p, 1001000);
    assert(p.due_ms == 1002000 && p.expires_ms == 1090000);
    assert(network_retry_step(&p, 1001999) == NET_RETRY_NONE);
    assert(network_retry_step(&p, 1002000) == NET_RETRY_CONNECT);
}

static void test_dhcp_recovery_is_bounded(void)
{
    network_retry_t p;
    network_retry_reset(&p, 0);
    assert(network_retry_step(&p, 0) == NET_RETRY_CONNECT);
    network_retry_associated(&p, 1000);
    network_retry_associated(&p, 15000);
    assert(p.due_ms == 16000 && p.expires_ms == 90000);
    assert(network_retry_step(&p, 15999) == NET_RETRY_NONE);
    assert(network_retry_step(&p, 16000) == NET_RETRY_RENEW_DHCP);
    assert(p.dhcp_renewals == 1 && p.due_ms == 31000);
    network_retry_associated(&p, 30000);
    assert(p.due_ms == 31000);
    assert(network_retry_step(&p, 30999) == NET_RETRY_NONE);
    assert(network_retry_step(&p, 31000) == NET_RETRY_RESTART_RADIO);
    assert(p.phase == NET_RETRY_WAIT && p.radio_restarts == 1);
    assert(p.due_ms == 33000 && p.expires_ms == 90000);
    network_retry_disconnected(&p, 31900);
    network_retry_associated(&p, 32000);
    network_retry_online(&p);
    assert(p.phase == NET_RETRY_WAIT && p.due_ms == 33000);
    assert(network_retry_step(&p, 32999) == NET_RETRY_NONE);
    assert(network_retry_step(&p, 33000) == NET_RETRY_CONNECT);
    network_retry_associated(&p, 34000);
    assert(network_retry_step(&p, 48999) == NET_RETRY_NONE);
    assert(network_retry_step(&p, 49000) == NET_RETRY_STOP);
    assert(p.attempts == 2 && p.phase == NET_RETRY_FAILED);
    assert(p.dhcp_renewals == 1 && p.radio_restarts == 1);

    network_retry_associated(&p, 50000);
    network_retry_online(&p);
    network_retry_disconnected(&p, 51000);
    assert(network_retry_step(&p, 500000) == NET_RETRY_NONE);
    assert(p.phase == NET_RETRY_FAILED);
    network_retry_reset(&p, 600000);
    assert(p.expires_ms == 690000 && p.attempts == 0);
    assert(network_retry_step(&p, 600000) == NET_RETRY_CONNECT);
}

static void test_association_deadline_and_attempt_limit(void)
{
    network_retry_t p;
    network_retry_reset(&p, 0);
    assert(network_retry_step(&p, 0) == NET_RETRY_CONNECT);
    assert(network_retry_step(&p, 19999) == NET_RETRY_NONE);
    assert(network_retry_step(&p, 20000) == NET_RETRY_DISCONNECT);
    assert(p.due_ms == 22000);
    network_retry_disconnected(&p, 20100);
    assert(p.due_ms == 22000);
    assert(network_retry_step(&p, 21999) == NET_RETRY_NONE);
    assert(network_retry_step(&p, 22000) == NET_RETRY_CONNECT);
    network_retry_disconnected(&p, 23000);
    assert(network_retry_step(&p, 24999) == NET_RETRY_NONE);
    assert(network_retry_step(&p, 25000) == NET_RETRY_CONNECT);
    assert(p.attempts == 3);
    network_retry_disconnected(&p, 26000);
    assert(network_retry_step(&p, 26000) == NET_RETRY_STOP);
    assert(network_retry_step(&p, 1000000) == NET_RETRY_NONE);
}

static void test_backoff_starts_at_failure(void)
{
    network_retry_t p;
    network_retry_reset(&p, 1000);
    assert(network_retry_step(&p, 1000) == NET_RETRY_CONNECT);
    network_retry_disconnected(&p, 19000);
    assert(p.due_ms == 21000 && p.expires_ms == 91000);
    network_retry_disconnected(&p, 20500);
    assert(p.due_ms == 21000);
    assert(network_retry_step(&p, 20999) == NET_RETRY_NONE);
    assert(network_retry_step(&p, 21000) == NET_RETRY_CONNECT);
}

static void test_total_budget_overrides_stage_deadlines(void)
{
    network_retry_t p;
    network_retry_reset(&p, 1000);
    /* A stopped or delayed owner does not buy a fresh 90-second budget. */
    assert(network_retry_step(&p, 90000) == NET_RETRY_CONNECT);
    network_retry_associated(&p, 90001);
    assert(p.due_ms == 105001 && p.expires_ms == 91000);
    assert(network_retry_step(&p, 90999) == NET_RETRY_NONE);
    assert(network_retry_step(&p, 91000) == NET_RETRY_STOP);

    network_retry_reset(&p, 0);
    assert(network_retry_step(&p, 90000) == NET_RETRY_STOP);
    assert(p.attempts == 0);

    network_retry_reset(&p, 0);
    assert(network_retry_step(&p, 89000) == NET_RETRY_CONNECT);
    network_retry_disconnected(&p, 89999);
    assert(p.due_ms == 91999);
    assert(network_retry_step(&p, 90000) == NET_RETRY_STOP);
}

static void test_recovery_success_and_late_owner(void)
{
    network_retry_t p;
    network_retry_reset(&p, 0);
    assert(network_retry_step(&p, 0) == NET_RETRY_CONNECT);
    network_retry_associated(&p, 1);
    assert(network_retry_step(&p, 20000) == NET_RETRY_RENEW_DHCP);
    /* The next DHCP wait starts when renewal is actually requested. */
    assert(p.due_ms == 35000);
    network_retry_online(&p);
    assert(network_retry_step(&p, 90000) == NET_RETRY_NONE);
    network_retry_disconnected(&p, 100000);
    assert(p.dhcp_renewals == 0 && p.expires_ms == 190000);
    assert(network_retry_step(&p, 102000) == NET_RETRY_CONNECT);
    network_retry_associated(&p, 102100);
    assert(network_retry_step(&p, 117100) == NET_RETRY_RENEW_DHCP);
    assert(network_retry_step(&p, 132100) == NET_RETRY_RESTART_RADIO);
    /* Caller can hold WAIT until real radio STOP/START completes. */
    assert(network_retry_step(&p, 140000) == NET_RETRY_CONNECT);
    assert(p.due_ms == 160000);
    network_retry_associated(&p, 141000);
    network_retry_online(&p);
    assert(p.phase == NET_RETRY_ONLINE);
    /* A repeated association while online must not start another DHCP wait. */
    network_retry_associated(&p, 150000);
    assert(network_retry_step(&p, 151000) == NET_RETRY_NONE);
    assert(p.phase == NET_RETRY_ONLINE);

    /* Both recovery options were consumed before success. A later real link
     * loss must replenish both, rather than failing on its first DHCP timeout. */
    network_retry_disconnected(&p, 200000);
    assert(p.attempts == 0 && p.dhcp_renewals == 0 && p.radio_restarts == 0);
    assert(p.expires_ms == 290000);
    assert(network_retry_step(&p, 202000) == NET_RETRY_CONNECT);
    network_retry_associated(&p, 202001);
    assert(network_retry_step(&p, 217001) == NET_RETRY_RENEW_DHCP);
    assert(network_retry_step(&p, 232001) == NET_RETRY_RESTART_RADIO);
}

static void test_counters_survive_failed_connections(void)
{
    network_retry_t p;
    network_retry_reset(&p, 0);
    assert(network_retry_step(&p, 0) == NET_RETRY_CONNECT);
    network_retry_associated(&p, 1);
    assert(network_retry_step(&p, 15001) == NET_RETRY_RENEW_DHCP);
    network_retry_disconnected(&p, 16000);
    assert(network_retry_step(&p, 18000) == NET_RETRY_CONNECT);
    assert(p.dhcp_renewals == 1 && p.expires_ms == 90000);
    network_retry_associated(&p, 18001);
    assert(network_retry_step(&p, 33001) == NET_RETRY_RESTART_RADIO);
    assert(network_retry_step(&p, 35001) == NET_RETRY_CONNECT);
    network_retry_associated(&p, 35002);
    assert(network_retry_step(&p, 50002) == NET_RETRY_STOP);
    assert(p.attempts == 3 && p.radio_restarts == 1);
}

static void test_no_radio_restart_without_connect_budget(void)
{
    network_retry_t p;
    network_retry_reset(&p, 0);
    assert(network_retry_step(&p, 0) == NET_RETRY_CONNECT);
    network_retry_disconnected(&p, 1);
    assert(network_retry_step(&p, 2001) == NET_RETRY_CONNECT);
    network_retry_disconnected(&p, 2002);
    assert(network_retry_step(&p, 4002) == NET_RETRY_CONNECT);
    network_retry_associated(&p, 4003);
    assert(network_retry_step(&p, 19003) == NET_RETRY_RENEW_DHCP);
    assert(network_retry_step(&p, 34003) == NET_RETRY_STOP);
    assert(p.attempts == 3 && p.radio_restarts == 0);
}

static void test_scan_time_consumes_existing_budget(void)
{
    network_retry_t p;
    network_retry_reset(&p, 0);
    assert(network_retry_step(&p, 0) == NET_RETRY_CONNECT);
    network_retry_associated(&p, 1000);
    /* Starting a manual scan cancels the unfinished connection. The owner
     * suppresses CONNECT while scanning; this is policy time, not a scan mock. */
    network_retry_disconnected(&p, 5000);
    assert(p.phase == NET_RETRY_WAIT && p.due_ms == 7000);
    network_retry_disconnected(&p, 5100);
    network_retry_associated(&p, 5200);
    assert(p.expires_ms == 90000 && p.due_ms == 7000);

    /* Scans must not create a new recovery episode when the owner resumes. */
    assert(network_retry_step(&p, 90000) == NET_RETRY_STOP);
    assert(p.attempts == 1);
    network_retry_online(&p);
    network_retry_associated(&p, 90100);
    network_retry_disconnected(&p, 90200);
    assert(network_retry_step(&p, 180000) == NET_RETRY_NONE);
    assert(p.phase == NET_RETRY_FAILED && p.expires_ms == 90000);
}

static void test_last_allowed_connection_can_succeed(void)
{
    network_retry_t p;
    network_retry_reset(&p, 0);
    assert(network_retry_step(&p, 0) == NET_RETRY_CONNECT);
    network_retry_disconnected(&p, 1000);
    assert(network_retry_step(&p, 3000) == NET_RETRY_CONNECT);
    network_retry_disconnected(&p, 4000);
    assert(network_retry_step(&p, 6000) == NET_RETRY_CONNECT);
    assert(p.attempts == 3);
    network_retry_associated(&p, 7000);
    assert(network_retry_step(&p, 8000) == NET_RETRY_NONE);
    network_retry_online(&p);
    assert(network_retry_step(&p, 1000000) == NET_RETRY_NONE);
    assert(p.phase == NET_RETRY_ONLINE);

    network_retry_disconnected(&p, 1001000);
    assert(network_retry_step(&p, 1002999) == NET_RETRY_NONE);
    assert(network_retry_step(&p, 1003000) == NET_RETRY_CONNECT);
    assert(p.attempts == 1);
}

static void test_monotonic_clock_above_32_bits(void)
{
    const int64_t start = (INT64_C(1) << 32) - 1000;
    network_retry_t p;
    network_retry_reset(&p, start);
    assert(network_retry_step(&p, start) == NET_RETRY_CONNECT);
    network_retry_associated(&p, start + 500);
    assert(network_retry_step(&p, start + 15499) == NET_RETRY_NONE);
    assert(network_retry_step(&p, start + 15500) == NET_RETRY_RENEW_DHCP);
    assert(network_retry_step(&p, start + 30500) == NET_RETRY_RESTART_RADIO);
    assert(network_retry_step(&p, start + 32499) == NET_RETRY_NONE);
    assert(network_retry_step(&p, start + 32500) == NET_RETRY_CONNECT);
    assert(network_retry_step(&p, start + 90000) == NET_RETRY_STOP);
}

int main(void)
{
    test_success_and_new_disconnect_budget();
    test_dhcp_recovery_is_bounded();
    test_association_deadline_and_attempt_limit();
    test_backoff_starts_at_failure();
    test_total_budget_overrides_stage_deadlines();
    test_recovery_success_and_late_owner();
    test_counters_survive_failed_connections();
    test_no_radio_restart_without_connect_budget();
    test_scan_time_consumes_existing_budget();
    test_last_allowed_connection_can_succeed();
    test_monotonic_clock_above_32_bits();
    puts("Network retry: 11 scenarios passed (scan budget, duplicate events, last-attempt success, recovery, long uptime)");
    return 0;
}
