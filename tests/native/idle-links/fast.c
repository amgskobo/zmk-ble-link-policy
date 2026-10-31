/* SPDX-License-Identifier: GPL-3.0-only */
/* The fast-interval fixture: the selected host is asked for 7..7 (8.75 ms), below the 15 ms that Apple hosts accept
 * (CMake -DZBLP_IDLE_LINKS_FAST=ON). Idle hosts must still be asked for 15 ms or more, and a selected host that applies
 * nothing below 15 ms is failed over to 12..12. The stand-ins are in harness.inc. Literals are the oracle. */
#include "harness.inc"

#define FAST 7
#define COMPAT 12

static void connection_with(unsigned i, uint16_t interval, uint16_t latency)
{
    connections[i].info.le.interval = interval;
    connections[i].info.le.latency = latency;
}

ZTEST(idle_links_fast, test_selected_host_asks_for_the_fast_range_and_idle_hosts_for_15ms)
{
    wake(); settle();
    zassert_equal(connections[0].requests, 1);
    zassert_equal(connections[0].requested_min, FAST);
    zassert_equal(connections[0].requested_max, FAST);
    zassert_equal(connections[0].last_requested_latency, 0);
    zassert_equal(connections[0].info.le.interval, FAST);
    for (unsigned i = 1; i < 5; i++) {
        zassert_equal(connections[i].requests, 1);
        zassert_equal(connections[i].requested_min, COMPAT);
        zassert_equal(connections[i].requested_max, COMPAT);
        zassert_equal(connections[i].last_requested_latency, 30);
        zassert_equal(connections[i].info.le.interval, COMPAT);
        zassert_equal(connections[i].info.le.latency, 30);
    }
    for (unsigned i = 5; i < 9; i++) { zassert_equal(connections[i].requests, 0); }
    zassert_false(policy.slot[0].fail_over);
    zassert_false(k_work_delayable_is_pending(&link_work));
}

ZTEST(idle_links_fast, test_wide_idle_request_keeps_the_15ms_floor)
{
    confirm_request = false;
    wake(); settle();
    k_msleep(20100);
    for (unsigned i = 1; i < 5; i++) {
        zassert_equal(connections[i].requests, 2);
        zassert_equal(connections[i].requested_min, COMPAT);
        zassert_equal(connections[i].requested_max, 24);
    }
    /* The selected request is not an idle one: it stays 7..7 for its second request. */
    zassert_equal(connections[0].requests, 2);
    zassert_equal(connections[0].requested_min, FAST);
    zassert_equal(connections[0].requested_max, FAST);
}

ZTEST(idle_links_fast, test_a_host_that_applies_nothing_is_failed_over_to_15ms)
{
    selected.ble.profile_index = 4;
    confirm_request = false;
    wake(); settle();
    zassert_equal(connections[4].requests, 1);
    zassert_equal(connections[4].requested_min, FAST);
    zassert_equal(connections[4].requested_max, FAST);
    k_msleep(20100);
    zassert_equal(connections[4].requests, 2);
    zassert_false(policy.slot[4].fail_over);
    /* Its second request is still settling for 20 s; the link (12, 0) is already inside the compatible range. */
    k_msleep(10000);
    zassert_false(policy.slot[4].fail_over);
    k_msleep(10100);
    zassert_true(policy.slot[4].fail_over);
    zassert_equal(connections[4].requests, 2);
    zassert_equal(policy.slot[4].attempts, 0);
    /* Quiet from then on: a host at 12 units / latency 0 is asked for nothing more. */
    k_msleep(60000);
    zassert_equal(connections[4].requests, 2);
    zassert_true(policy.slot[4].fail_over);
}

ZTEST(idle_links_fast, test_a_failed_over_host_outside_the_15ms_range_is_asked_for_it)
{
    selected.ble.profile_index = 4;
    confirm_request = false;
    connection_with(4, 24, 0); /* it sits at 30 ms */
    wake(); settle();
    k_msleep(40100);
    zassert_true(policy.slot[4].fail_over);
    zassert_equal(connections[4].requests, 3);
    zassert_equal(connections[4].requested_min, COMPAT);
    zassert_equal(connections[4].requested_max, COMPAT);
    zassert_equal(connections[4].last_requested_latency, 0);
}

ZTEST(idle_links_fast, test_a_host_that_applies_the_second_request_is_not_failed_over)
{
    ignore_requests = 1; /* Windows applied nothing until the retry 20 s later */
    wake(); settle();
    zassert_equal(connections[0].requests, 1);
    zassert_equal(connections[0].info.le.interval, COMPAT); /* nothing applied yet */
    k_msleep(20100);
    zassert_equal(connections[0].requests, 2);
    zassert_equal(connections[0].info.le.interval, FAST);
    zassert_equal(connections[0].info.le.latency, 0);
    k_msleep(60000);
    zassert_equal(connections[0].requests, 2);
    zassert_false(policy.slot[0].fail_over);
}

ZTEST(idle_links_fast, test_failover_ends_with_the_connection)
{
    selected.ble.profile_index = 4;
    confirm_request = false;
    wake(); settle();
    k_msleep(40100);
    zassert_true(policy.slot[4].fail_over);
    connections[4].info.state = BT_CONN_STATE_DISCONNECTED;
    disconnected(&connections[4], 0); settle();
    zassert_false(policy.slot[4].fail_over);
    zassert_false(policy.slot[4].mode_valid);
    /* The next connection of the profile starts from the configured range again. */
    connections[4].info.state = BT_CONN_STATE_CONNECTED;
    connection_with(4, COMPAT, 0);
    const unsigned before_requests = connections[4].requests;
    wake(); settle();
    zassert_equal(connections[4].requests, before_requests + 1);
    zassert_equal(connections[4].requested_min, FAST);
    zassert_equal(connections[4].requested_max, FAST);
}

ZTEST(idle_links_fast, test_observation_uses_the_range_the_host_was_asked_for)
{
    selected.ble.profile_index = 4;
    confirm_request = false;
    wake(); settle();
    /* Not failed over yet (slot 2 is idle but its fast values are the configured ones): 7 units is fast, 12 is not. */
    parameters(&connections[2], 12, 0, 400);
    zassert_false(policy.slot[2].observed_fast);
    parameters(&connections[2], FAST, 0, 400);
    zassert_true(policy.slot[2].observed_fast);
    k_msleep(40100);
    zassert_true(policy.slot[4].fail_over);
    /* Failed over: the compatible range is the fast one for that connection, and only for it. */
    parameters(&connections[4], COMPAT, 0, 400);
    zassert_true(policy.slot[4].observed_fast);
    parameters(&connections[4], FAST, 0, 400);
    zassert_false(policy.slot[4].observed_fast);
    parameters(&connections[3], COMPAT, 0, 400);
    zassert_false(policy.slot[3].observed_fast);
}

ZTEST(idle_links_fast, test_idle_observation_needs_the_15ms_floor)
{
    wake(); settle();
    parameters(&connections[1], 8, 30, 400);
    zassert_false(policy.slot[1].observed_idle);
    parameters(&connections[1], COMPAT, 30, 400);
    zassert_true(policy.slot[1].observed_idle);
    parameters(&connections[1], 24, 30, 400);
    zassert_true(policy.slot[1].observed_idle);
    parameters(&connections[1], 25, 30, 400);
    zassert_false(policy.slot[1].observed_idle);
}

/* A fast idle link of a host that took the 7..7 idle request of the old policy is pulled back to 15 ms. */
ZTEST(idle_links_fast, test_an_idle_host_at_the_fast_interval_is_moved_to_15ms)
{
    connection_with(2, FAST, 30);
    wake(); settle();
    zassert_equal(connections[2].requests, 1);
    zassert_equal(connections[2].requested_min, COMPAT);
    zassert_equal(connections[2].last_requested_latency, 30);
    zassert_equal(connections[2].info.le.interval, COMPAT);
}

ZTEST_SUITE(idle_links_fast, NULL, NULL, before, NULL, NULL);
