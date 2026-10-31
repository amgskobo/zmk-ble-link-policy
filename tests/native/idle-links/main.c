/* SPDX-License-Identifier: GPL-3.0-only */
/* The default fixture: selected host asked for 12..12 (the production range). The stand-ins are in harness.inc. */
#include "harness.inc"

ZTEST(idle_links, test_selection_during_info_invalidates_first_request)
{
    selected.ble.profile_index = 4;
    connections[4].info.le.latency = 30;
    connections[3].info.le.latency = 30;
    inject_info_selection = true;
    wake();
    k_msleep(30);
    zassert_false(inject_info_selection);
    zassert_true(total > 0);
    zassert_equal(request_order[0], 3);
    zassert_equal(connections[3].last_requested_latency, 0);
    zassert_equal(connections[4].requests, 0);
}

ZTEST(idle_links, test_worker_yields_after_each_request)
{
    selected.ble.profile_index = 4;
    connections[4].info.le.latency = 30;
    confirm_request = false;
    probe_enabled = true;
    wake();
    k_msleep(30);
    /* The unrelated queued item must run between update calls. Merely
     * counting API entries after a complete batch cannot prove this. */
    zassert_equal(total, 5);
    zassert_equal(request_order[0], 4);
    zassert_equal(probe_count, 5);
    for (unsigned i = 0; i < 5; i++) {
        zassert_equal(probe_samples[i], i + 1);
        zassert_equal(connections[i].requests, 1);
        zassert_equal(policy.slot[i].attempts, 1);
    }
}

ZTEST(idle_links, test_yielded_worker_rechecks_new_selection)
{
    selected.ble.profile_index = 4;
    connections[4].info.le.latency = 30;
    connections[3].info.le.latency = 30;
    confirm_request = false;
    probe_enabled = true;
    probe_reselect = true;
    wake();
    k_msleep(30);
    zassert_false(probe_reselect);
    zassert_equal(request_order[0], 4);
    zassert_equal(probe_samples[0], 1);
    zassert_true(total >= 2);
    zassert_equal(request_order[1], 3);
    zassert_equal(connections[3].last_requested_latency, 0);
}

ZTEST(idle_links, test_diagnostic_clock_wrap_and_ambiguous_span)
{
#if defined(CONFIG_ZMK_BLE_LINK_POLICY_DIAGNOSTICS)
    bool valid;
    const uint32_t value = elapsed_us(UINT32_MAX - 9U, 10U, &valid);
    zassert_true(valid);
    zassert_equal(value, UINT64_C(20) * 1000000U / CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC);
    zassert_equal(elapsed_us(100, 100, &valid), 0);
    zassert_true(valid);
    zassert_equal(elapsed_us(0, UINT32_C(0x80000000), &valid), 0);
    zassert_false(valid);
#else
    ztest_test_skip();
#endif
}

ZTEST(idle_links, test_irrelevant_parameters_do_not_schedule)
{
    wake(); settle();
    const uint64_t generation = wake_generation;
    const unsigned requests = total;
    const struct zblp_link_policy saved = policy;
    /* This boundary used to share the Messenger traffic scenario. Keep an
     * independent owner assertion after the application observer moves out. */
    for (unsigned repeat = 0; repeat < 100; repeat++) {
        for (unsigned i = 5; i < 9; i++) {
            parameters(&connections[i], 12, 0, 400);
        }
    }
    info_error_conn = &connections[1];
    parameters(&connections[1], 12, 0, 400);
    info_error_conn = NULL;
    zassert_equal(wake_generation, generation);
    zassert_equal(total, requests);
    zassert_mem_equal(&policy, &saved, sizeof(saved));
    zassert_false(k_work_delayable_is_pending(&link_work));
}

ZTEST(idle_links, test_irrelevant_lifecycle_callbacks_do_not_schedule)
{
    wake(); settle();
    const uint64_t generation = wake_generation;
    const unsigned scans = scan_serial;
    const unsigned requests = total;
    const struct zblp_link_policy saved = policy;
    for (unsigned repeat = 0; repeat < 100; repeat++) {
        for (unsigned i = 5; i < 8; i++) {
            connected(&connections[i], 0);
            security(&connections[i], BT_SECURITY_L2, 0);
            disconnected(&connections[i], 0);
        }
        connected(&connections[8], 0);
        security(&connections[8], BT_SECURITY_L2, 0);
        connected(&connections[1], 1);
    }
    settle();
    printk("ignored lifecycle events=1200 wakes=%u scans=%u requests=%u\n",
           (unsigned)(wake_generation - generation), scan_serial - scans, total - requests);
    zassert_equal(wake_generation, generation);
    zassert_equal(scan_serial, scans);
    zassert_equal(total, requests);
    zassert_mem_equal(&policy, &saved, sizeof(policy));
    zassert_false(k_work_delayable_is_pending(&link_work));
}

ZTEST(idle_links, test_unreadable_lifecycle_callbacks_do_not_schedule)
{
    wake(); settle();
    const uint64_t generation = wake_generation;
    const unsigned scans = scan_serial;
    info_error_conn = &connections[1];
    connected(&connections[1], 0);
    security(&connections[1], BT_SECURITY_L2, 0);
    disconnected(&connections[1], 0);
    settle();
    zassert_equal(wake_generation, generation);
    zassert_equal(scan_serial, scans);
    zassert_false(k_work_delayable_is_pending(&link_work));
}

ZTEST(idle_links, test_known_host_lifecycle_still_wakes_and_resets)
{
    wake(); settle();
    uint64_t generation = wake_generation;
    connected(&connections[1], 0);
    zassert_equal(wake_generation, generation + 1);
    settle();
    generation = wake_generation;
    security(&connections[1], BT_SECURITY_L2, 0);
    zassert_equal(wake_generation, generation + 1);
    settle();
    policy.slot[1].attempts = 2;
    connections[1].info.state = BT_CONN_STATE_DISCONNECTED;
    generation = wake_generation;
    disconnected(&connections[1], 0);
    zassert_equal(wake_generation, generation + 1);
    zassert_equal(policy.slot[1].attempts, 0);
    settle();
    zassert_false(k_work_delayable_is_pending(&link_work));
}

ZTEST(idle_links, test_five_hosts_and_split_exclusion)
{
    endpoint_changed(NULL);
    settle();
    zassert_equal(connections[0].requests, 0);
    for (unsigned i = 1; i < 5; i++) {
        zassert_equal(connections[i].info.le.latency, 30);
        zassert_equal(connections[i].requests, 1);
    }
    for (unsigned i = 5; i < 9; i++) { zassert_equal(connections[i].requests, 0); }
    zassert_false(k_work_delayable_is_pending(&link_work));
    selected.ble.profile_index = 2;
    endpoint_changed(NULL);
    settle();
    k_msleep(6100);
    zassert_equal(connections[0].info.le.latency, 30);
    zassert_equal(connections[2].info.le.latency, 0);
    selected.transport = ZMK_TRANSPORT_USB;
    endpoint_changed(NULL);
    settle();
    k_msleep(6100);
    zassert_equal(connections[2].info.le.latency, 30);
}



ZTEST(idle_links, test_api_success_without_confirmation_is_bounded)
{
    confirm_request = false;
    wake(); settle();
    k_msleep(61100);
    for (unsigned i = 1; i < 5; i++) {
        zassert_equal(connections[i].requests, 3);
        zassert_equal(connections[i].info.le.latency, 0);
    }
    zassert_false(k_work_delayable_is_pending(&link_work));
    connections[1].info.le.latency = 30;
    parameters(&connections[1], 12, 30, 400); settle();
    zassert_equal(policy.slot[1].attempts, 0);
    zassert_false(k_work_delayable_is_pending(&link_work));
}

ZTEST(idle_links, test_selection_after_old_scan_is_not_cancelled)
{
    /* Every old parameter already matches, so that old pass would cancel its
     * timer. Insert the new endpoint wake after it collected all old facts. */
    for (unsigned i = 1; i < 5; i++) { connections[i].info.le.latency = 30; }
    inject_after_scan = true;
    wake(); settle();
    k_msleep(6100);
    zassert_false(inject_after_scan);
    zassert_equal(selected.ble.profile_index, 1);
    zassert_equal(connections[0].info.le.latency, 30);
    zassert_equal(connections[1].info.le.latency, 0);
    zassert_false(k_work_delayable_is_pending(&link_work));
}

ZTEST(idle_links, test_new_selection_supersedes_stored_initial_preference)
{
    confirm_request = false;
    wake(); settle();
    zassert_equal(connections[1].info.le.latency, 0);
    zassert_equal(connections[1].pending_latency, 30);
    selected.ble.profile_index = 1;
    endpoint_changed(NULL); settle();
    /* A stored idle request stays unconfirmed. Latest selection survives the
     * bounded settlement wait even when current fast values already match. */
    zassert_equal(connections[1].pending_latency, 30);
    zassert_equal(connections[1].requests, 1);
    k_msleep(20000);
    zassert_equal(connections[1].pending_latency, 0);
    zassert_equal(connections[1].requests, 2);
    /* Simulate the stack's later initial update using its latest stored value. */
    connections[1].info.le.latency = connections[1].pending_latency;
    parameters(&connections[1], 12, connections[1].info.le.latency, 400); settle();
    zassert_equal(connections[1].info.le.latency, 0);
}

ZTEST(idle_links, test_idle_scaled_interval_is_confirmed)
{
    /* A peer's accepted 30 ms/latency30 is already idle, not a refusal. */
    for (unsigned i = 1; i < 5; i++) {
        connections[i].info.le.interval = 24;
        connections[i].info.le.latency = 30;
    }
    wake(); settle();
    zassert_equal(total, 0);
    zassert_false(k_work_delayable_is_pending(&link_work));

    /* A peer's actual idle interval may exceed the preferred request. */
    connections[1].info.le.latency = 0;
    accepted_interval = 24;
    wake(); settle();
    zassert_equal(connections[1].requests, 1);
    zassert_equal(connections[1].info.le.latency, 30);
    zassert_false(k_work_delayable_is_pending(&link_work));

    /* Values outside either interval boundary are not confirmed as idle. */
    connections[2].info.le.interval = 25;
    connections[3].info.le.interval = 11;
    wake(); settle();
    zassert_equal(connections[2].requests, 1);
    zassert_equal(connections[3].requests, 1);
    zassert_false(k_work_delayable_is_pending(&link_work));

    /* Selection restores the original 15 ms/latency0 preference. */
    accepted_interval = 0;
    selected.ble.profile_index = 1;
    endpoint_changed(NULL); settle();
    k_msleep(5000);
    zassert_equal(connections[1].info.le.interval, 12);
    zassert_equal(connections[1].info.le.latency, 0);
}

ZTEST(idle_links, test_mode_churn_retains_gate_and_failed_supersede)
{
    confirm_request = false;
    wake(); settle();
    zassert_equal(connections[1].requests, 1);
    request_error = -EACCES;
    for (unsigned i = 0; i < 40; i++) {
        selected.ble.profile_index = (i % 2U) ? 1 : 0;
        endpoint_changed(NULL);
        k_msleep(100);
    }
    zassert_equal(connections[1].requests, 1);
    selected.ble.profile_index = 1;
    endpoint_changed(NULL);
    k_msleep(16100);
    zassert_equal(connections[1].requests, 2);
    zassert_equal(connections[1].pending_latency, 30);
    zassert_false(policy.slot[1].preference_fast);
    /* A refused superseding request must not quiesce on the currently fast
     * observation: the stack still holds the old idle preference. */
    wake(); settle();
    zassert_true(k_work_delayable_is_pending(&link_work));
    request_error = 0;
    k_msleep(5000);
    zassert_equal(connections[1].requests, 3);
    zassert_equal(connections[1].pending_latency, 0);
    zassert_true(policy.slot[1].preference_fast);
    parameters(&connections[1], 12, 0, 400);
    wake(); settle();
    zassert_equal(policy.slot[1].attempts, 0);
}

ZTEST(idle_links, test_disconnect_during_request_cannot_restore_preference)
{
    inject_disconnect = true;
    wake(); settle();
    zassert_false(inject_disconnect);
    zassert_equal(connections[1].info.state, BT_CONN_STATE_DISCONNECTED);
    zassert_false(policy.slot[1].preference_valid);
    zassert_false(policy.slot[1].mode_valid);
}

ZTEST(idle_links, test_no_duplicate_while_peer_update_is_slow)
{
    confirm_request = false;
    wake(); settle();
    /* An unrelated/out-of-range parameter notification is not completion. */
    parameters(&connections[1], 25, 30, 400); settle();
    k_msleep(8100);
    for (unsigned i = 1; i < 5; i++) { zassert_equal(connections[i].requests, 1); }
    connections[1].info.le.latency = 30;
    parameters(&connections[1], 12, 30, 400); settle();
    zassert_equal(connections[1].requests, 1);
}

ZTEST(idle_links, test_confirmed_preference_does_not_force_matching_reversal)
{
    wake(); settle();
    zassert_equal(connections[1].requests, 1);
    /* The peer has already restored fast parameters before selection. Neither
     * its completed idle request nor its later fast callback is pending. */
    connections[1].info.le.latency = 0;
    parameters(&connections[1], 12, 0, 400);
    selected.ble.profile_index = 1;
    endpoint_changed(NULL); settle();
    k_msleep(5100);
    zassert_equal(connections[1].requests, 1);
}

ZTEST(idle_links, test_selected_host_request_precedes_idle_updates)
{
    selected.ble.profile_index = 4;
    connections[4].info.le.latency = 30;
    wake(); settle();
    zassert_true(total > 0);
    zassert_equal(request_order[0], 4);
}

ZTEST(idle_links, test_short_deselection_does_not_request_idle)
{
    for (unsigned i = 1; i < 5; i++) { connections[i].info.le.latency = 30; }
    wake(); settle();
    selected.ble.profile_index = 1;
    endpoint_changed(NULL); settle();
    k_msleep(100);
    selected.ble.profile_index = 0;
    endpoint_changed(NULL); settle();
    k_msleep(500);
    zassert_equal(connections[0].requests, 0);
    zassert_equal(connections[0].info.le.latency, 0);
}

ZTEST(idle_links, test_selection_during_request_stops_old_scan)
{
    inject_selection = true;
    wake(); settle();
    zassert_false(inject_selection);
    zassert_true(total > 0 && total < ARRAY_SIZE(request_scan));
    zassert_equal(request_order[0], 1);
    for (unsigned i = 1; i < total; i++) {
        zassert_not_equal(request_scan[i], request_scan[0]);
    }
}

/* Nothing holds a host's first idle request back while the selected host is still being promoted:
 * the selected host is queued first, every other host follows in the same pass. */
ZTEST(idle_links, test_idle_requests_do_not_wait_for_selected_confirmation)
{
    selected.ble.profile_index = 4;
    connections[4].info.le.latency = 30;
    confirm_request = false;
    wake(); settle();
    zassert_equal(request_order[0], 4);
    zassert_equal(connections[4].requests, 1);
    zassert_equal(connections[4].last_requested_latency, 0);
    for (unsigned i = 0; i < 4; i++) {
        zassert_equal(connections[i].requests, 1);
        zassert_equal(connections[i].last_requested_latency, 30);
    }
}

/* The demotion of a previous host is a fixed six seconds after the last selection change; every
 * change restarts it. Promotion of the new host is immediate. Literals are the independent oracle. */
ZTEST(idle_links, test_demotion_is_six_seconds_after_the_last_selection_change)
{
    for (unsigned i = 1; i < 5; i++) { connections[i].info.le.latency = 30; }
    wake(); settle();
    zassert_equal(total, 0);
    selected.ble.profile_index = 1;
    endpoint_changed(NULL); settle();
    zassert_equal(connections[1].requests, 1);
    zassert_equal(connections[1].last_requested_latency, 0);
    k_msleep(5000);
    zassert_equal(connections[0].requests, 0);
    selected.ble.profile_index = 2;
    endpoint_changed(NULL); settle();
    zassert_equal(connections[2].requests, 1);
    k_msleep(5900);
    /* Without the restart host 0 would have been demoted already, six seconds after the first change. */
    zassert_equal(connections[0].requests, 0);
    zassert_equal(connections[1].requests, 1);
    k_msleep(300);
    zassert_equal(connections[0].requests, 1);
    zassert_equal(connections[0].last_requested_latency, 30);
    zassert_equal(connections[1].requests, 2);
    zassert_equal(connections[1].last_requested_latency, 30);
    zassert_false(k_work_delayable_is_pending(&link_work));
}

/* An idle request starts at the preferred 12..12. A connection whose idle request went unconfirmed asks for the
 * wider 12..24 from its next attempt on, and keeps doing so until it disconnects. The selected host always
 * asks for exactly 12..12. Measured on hardware: a Mac applied 138 of 141 idle requests of 12..24 but only
 * 3 of 6 of 12..12, while Windows promotes faster when its idle link stays at 15 ms. */
ZTEST(idle_links, test_idle_request_is_narrow_first_then_wide_after_an_unconfirmed_attempt)
{
    confirm_request = false;
    wake(); settle();
    for (unsigned i = 1; i < 5; i++) {
        zassert_equal(connections[i].requests, 1);
        zassert_equal(connections[i].requested_min, 12);
        zassert_equal(connections[i].requested_max, 12);
    }
    k_msleep(20100);
    for (unsigned i = 1; i < 5; i++) {
        zassert_equal(connections[i].requests, 2);
        zassert_equal(connections[i].requested_min, 12);
        zassert_equal(connections[i].requested_max, 24);
    }
    k_msleep(20000);
    for (unsigned i = 1; i < 5; i++) {
        zassert_equal(connections[i].requests, 3);
        zassert_equal(connections[i].requested_max, 24);
    }
    /* The selected host asks for 12..12 even though its connection is flagged wide. The stored idle
     * request is still unconfirmed, so the promotion waits for its 20 s settlement. */
    confirm_request = true;
    selected.ble.profile_index = 1;
    endpoint_changed(NULL); settle();
    k_msleep(20000);
    zassert_equal(connections[1].requests, 4);
    zassert_equal(connections[1].last_requested_latency, 0);
    zassert_equal(connections[1].requested_min, 12);
    zassert_equal(connections[1].requested_max, 12);
    /* The flag is sticky: the next idle episode of that connection starts wide. */
    selected.ble.profile_index = 0;
    endpoint_changed(NULL); settle();
    k_msleep(6100);
    zassert_equal(connections[1].requests, 5);
    zassert_equal(connections[1].last_requested_latency, 30);
    zassert_equal(connections[1].requested_max, 24);
    /* A new connection of the same profile starts narrow again. */
    connections[2].info.state = BT_CONN_STATE_DISCONNECTED;
    disconnected(&connections[2], 0); settle();
    connections[2].info.state = BT_CONN_STATE_CONNECTED;
    connections[2].info.le.latency = 0;
    wake(); settle();
    zassert_equal(connections[2].requests, 4);
    zassert_equal(connections[2].last_requested_latency, 30);
    zassert_equal(connections[2].requested_max, 12);
}

/* A selected range at or above the compatible minimum (the production 12..12) is never failed over: a host that
 * applies nothing keeps being asked within the budget of three, and the connection is not flagged. */
ZTEST(idle_links, test_a_selected_range_at_15ms_is_never_failed_over)
{
    confirm_request = false;
    connections[0].info.le.interval = 24; /* the selected host sits at 30 ms and ignores every request */
    wake(); settle();
    k_msleep(40100);
    zassert_equal(connections[0].requests, 3);
    zassert_equal(connections[0].requested_min, 12);
    zassert_equal(connections[0].requested_max, 12);
    zassert_false(policy.slot[0].fail_over);
}

ZTEST(idle_links, test_confirmed_first_idle_request_stays_narrow)
{
    wake(); settle();
    for (unsigned i = 1; i < 5; i++) {
        zassert_equal(connections[i].requests, 1);
        zassert_equal(connections[i].requested_max, 12);
        zassert_equal(connections[i].info.le.latency, 30);
    }
    selected.ble.profile_index = 1;
    endpoint_changed(NULL); settle();
    k_msleep(5100);
    zassert_equal(connections[1].requests, 2);
    zassert_equal(connections[1].last_requested_latency, 0);
    zassert_equal(connections[1].requested_max, 12);
    selected.ble.profile_index = 0;
    endpoint_changed(NULL); settle();
    k_msleep(6100);
    /* The first idle request of that connection was applied, so its next idle episode starts narrow too. */
    zassert_equal(connections[1].requests, 3);
    zassert_equal(connections[1].last_requested_latency, 30);
    zassert_equal(connections[1].requested_max, 12);
}

/* A selected request the peer answers with the idle values it already has (hardware: Windows repeated its
 * 15 ms/latency 30 link within a millisecond to three seconds of 5 of 12 first promotions and applied nothing
 * until the 20 s retry) is repeated once the five-second command gate allows it, not after the 20 s
 * settlement. Literals are the oracle. Hosts 1..4 start idle, host 0 is selected. */
static void idle_hosts_with_selected_zero(void)
{
    for (unsigned i = 1; i < 5; i++) { connections[i].info.le.latency = 30; }
    wake(); settle();
    zassert_equal(total, 0);
}

ZTEST(idle_links, test_idle_answer_inside_the_api_call_retries_after_the_command_gate)
{
    idle_hosts_with_selected_zero();
    echo_idle = 1;
    selected.ble.profile_index = 1;
    endpoint_changed(NULL); settle();
    zassert_equal(connections[1].requests, 1);
    zassert_equal(connections[1].last_requested_latency, 0);
    zassert_equal(connections[1].info.le.latency, 30);
    k_msleep(4800);
    zassert_equal(connections[1].requests, 1);
    k_msleep(400);
    zassert_equal(connections[1].requests, 2);
    zassert_equal(connections[1].info.le.latency, 0);
    zassert_equal(echo_idle, 0);
    k_msleep(15000);
    zassert_equal(connections[1].requests, 2);
}

ZTEST(idle_links, test_idle_answer_after_the_api_call_retries_after_the_command_gate)
{
    idle_hosts_with_selected_zero();
    confirm_request = false;
    selected.ble.profile_index = 1;
    endpoint_changed(NULL); settle();
    zassert_equal(connections[1].requests, 1);
    k_msleep(1000);
    /* The stack reports the unchanged idle link one second after the request. */
    parameters(&connections[1], 12, 30, 400); settle();
    zassert_equal(connections[1].requests, 1);
    k_msleep(3800);
    zassert_equal(connections[1].requests, 1);
    k_msleep(500);
    zassert_equal(connections[1].requests, 2);
    zassert_equal(connections[1].last_requested_latency, 0);
}

ZTEST(idle_links, test_idle_answer_after_the_gate_retries_at_once)
{
    idle_hosts_with_selected_zero();
    confirm_request = false;
    selected.ble.profile_index = 1;
    endpoint_changed(NULL); settle();
    k_msleep(7000);
    zassert_equal(connections[1].requests, 1);
    parameters(&connections[1], 12, 30, 400); settle();
    zassert_equal(connections[1].requests, 2);
}

ZTEST(idle_links, test_repeated_idle_answers_stay_within_three_attempts)
{
    idle_hosts_with_selected_zero();
    echo_idle = 10;
    selected.ble.profile_index = 1;
    endpoint_changed(NULL); settle();
    k_msleep(10300);
    zassert_equal(connections[1].requests, 3);
    k_msleep(40000);
    zassert_equal(connections[1].requests, 3);
    zassert_equal(connections[1].info.le.latency, 30);
    zassert_equal(echo_idle, 7);
    zassert_false(k_work_delayable_is_pending(&link_work));
}

ZTEST(idle_links, test_other_notifications_do_not_shorten_the_settlement)
{
    idle_hosts_with_selected_zero();
    confirm_request = false;
    selected.ble.profile_index = 1;
    endpoint_changed(NULL); settle();
    zassert_equal(connections[1].requests, 1);
    /* Neither the fast nor the idle values: an interval above the idle maximum, a latency of neither. */
    parameters(&connections[1], 25, 30, 400); settle();
    parameters(&connections[1], 12, 15, 400); settle();
    k_msleep(6100);
    /* Host 0 was demoted six seconds after the selection; its pending idle request is not answered by fast values. */
    zassert_equal(connections[0].requests, 1);
    zassert_equal(connections[0].last_requested_latency, 30);
    parameters(&connections[0], 12, 0, 400); settle();
    k_msleep(10000);
    zassert_equal(connections[1].requests, 1);
    zassert_equal(connections[0].requests, 1);
    k_msleep(5000);
    /* Only the 20 s settlements end it: host 1 at 20 s after its request, host 0 at 26 s. */
    zassert_equal(connections[1].requests, 2);
    zassert_equal(connections[0].requests, 1);
}

ZTEST(idle_links, test_one_info_query_per_known_connection_per_reconcile)
{
    /* No request or callback changes the generation during this scan. The
     * eight known-address objects include split, wrong-id and disconnected
     * aliases; only the five eligible connected hosts survive pruning. */
    for (unsigned i = 1; i < 5; i++) { connections[i].info.le.latency = 30; }
    wake(); settle();
    zassert_equal(scan_serial, 2);
    zassert_equal(info_queries, 8);
    zassert_equal(total, 0);
    zassert_false(k_work_delayable_is_pending(&link_work));
    for (unsigned i = 0; i < 5; i++) { zassert_true(policy.slot[i].mode_valid); }
}

ZTEST_SUITE(idle_links, NULL, NULL, before, NULL, NULL);
