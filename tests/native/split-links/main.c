/* SPDX-License-Identifier: MIT
 * Real Zephyr queues/timers/locks; Bluetooth and ZMK activity are recorded
 * boundaries. This fixture does not model a radio or establish physical power.
 */
#include <errno.h>
#include <string.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zmk/activity.h>
#include "split_policy.h"

struct bt_conn { struct bt_conn_info info; unsigned references, requests; };
static struct bt_conn connections[8];
static unsigned requests;
static int api_error;
static bool confirm, disconnect_in_api, activity_in_api, activity_in_scan;
static const struct bt_conn *unreadable;
static enum zmk_activity_state activity;
static uint16_t last_interval;
static uint32_t last_request_ms;

#if defined(ZBLP_SPLIT_PREEMPT_TEST)
static bool quiet_ready, inject_quiet;
static void observed_unlock(struct k_spinlock *lock, k_spinlock_key_t key);
static uint32_t observed_prepare(struct zblp_split_policy *p, uint32_t now,
                                 struct zblp_split_request *request);
K_SEM_DEFINE(producer_ready, 0, 1);
K_SEM_DEFINE(producer_done, 0, 1);
#define k_spin_unlock observed_unlock
#define zblp_split_policy_prepare observed_prepare
#endif
#include ZBLP_SPLIT_POWER_SOURCE
#if defined(ZBLP_SPLIT_PREEMPT_TEST)
#undef k_spin_unlock
#undef zblp_split_policy_prepare
static uint32_t observed_prepare(struct zblp_split_policy *p, uint32_t now,
                                 struct zblp_split_request *request) {
    uint32_t result = zblp_split_policy_prepare(p, now, request);
    if (inject_quiet && result == UINT32_MAX && k_current_get() == &k_sys_work_q.thread) {
        quiet_ready = true;
    }
    return result;
}
static void observed_unlock(struct k_spinlock *lock, k_spinlock_key_t key) {
    bool trigger = quiet_ready && inject_quiet && k_current_get() == &k_sys_work_q.thread;
    if (trigger) { inject_quiet = false; }
    k_spin_unlock(lock, key);
    if (trigger) { k_sem_give(&producer_ready); }
}
static void produce_activity(void *a, void *b, void *c) {
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    for (;;) {
        k_sem_take(&producer_ready, K_FOREVER);
        update_activity(ZMK_ACTIVITY_IDLE);
        k_sem_give(&producer_done);
    }
}
K_THREAD_DEFINE(activity_producer, 1024, produce_activity, NULL, NULL, NULL, 5, 0, 0);
#endif

#if !defined(ZBLP_SPLIT_ACTIVITY_TEST)
enum zmk_activity_state zmk_activity_get_state(void) { return activity; }
#endif
uint8_t bt_conn_index(const struct bt_conn *conn) { return (uint8_t)(conn - connections); }
struct bt_conn *bt_conn_ref(struct bt_conn *conn) { conn->references++; return conn; }
void bt_conn_unref(struct bt_conn *conn) { zassert_true(conn->references > 0); conn->references--; }
int bt_conn_get_info(const struct bt_conn *conn, struct bt_conn_info *info) {
    *info = conn->info;
    return conn == unreadable ? -EIO : 0;
}
void bt_conn_foreach(enum bt_conn_type type, void (*callback)(struct bt_conn *, void *), void *data) {
    zassert_equal(type, BT_CONN_TYPE_LE);
    for (unsigned i = 0; i < ARRAY_SIZE(connections); i++) { callback(&connections[i], data); }
    if (activity_in_scan) { activity_in_scan = false; update_activity(ZMK_ACTIVITY_ACTIVE); }
}
static void observed(uint16_t interval, uint16_t latency, uint16_t timeout) {
    connections[0].info.le.interval = interval;
    connections[0].info.le.latency = latency;
    connections[0].info.le.timeout = timeout;
    parameters_changed(&connections[0], interval, latency, timeout);
}
int bt_conn_le_param_update(struct bt_conn *conn, const struct bt_le_conn_param *parameters) {
    zassert_equal(k_current_get(), &k_sys_work_q.thread);
    zassert_equal(conn, &connections[0], "Only the right split peer may be changed");
    zassert_equal(parameters->interval_min, parameters->interval_max);
    zassert_equal(parameters->latency, 30);
    zassert_equal(parameters->timeout, 400);
    requests++; conn->requests++; last_interval = parameters->interval_min;
    last_request_ms = k_uptime_get_32();
    if (disconnect_in_api) {
        disconnect_in_api = false;
        conn->info.state = BT_CONN_STATE_DISCONNECTED;
        disconnected(conn, 0);
        return 0;
    }
    if (activity_in_api) { activity_in_api = false; update_activity(ZMK_ACTIVITY_ACTIVE); }
    if (!api_error && confirm) {
        observed(parameters->interval_min, parameters->latency, parameters->timeout);
    }
    return api_error;
}

static void settle(void) { k_msleep(10); }
static struct zblp_split_policy configured_policy;
static void *setup(void) {
    /* Preserve the production initializer so a wrong compiled default is
     * observable; reset only transient state between scenarios. */
    configured_policy = policy;
    return NULL;
}
static void before(void *fixture) {
    ARG_UNUSED(fixture);
    struct k_work_sync sync;
    k_work_cancel_delayable_sync(&split_work, &sync);
    memset(connections, 0, sizeof(connections));
    for (unsigned i = 0; i < ARRAY_SIZE(connections); i++) {
        connections[i].info = (struct bt_conn_info){.type = BT_CONN_TYPE_LE,
            .state = BT_CONN_STATE_CONNECTED, .role = BT_CONN_ROLE_PERIPHERAL,
            .id = BT_ID_DEFAULT};
    }
    connections[0].info.role = BT_CONN_ROLE_CENTRAL;
    connections[0].info.le.interval = 6;
    connections[0].info.le.latency = 30;
    connections[0].info.le.timeout = 400;
    connections[6].info.state = connections[7].info.state = BT_CONN_STATE_DISCONNECTED;
    policy = (struct zblp_split_policy){
        .active_interval = configured_policy.active_interval,
        .idle_interval = configured_policy.idle_interval,
        .latency = configured_policy.latency, .timeout = configured_policy.timeout};
    peer_index = UINT8_MAX; wake_generation = requests = last_request_ms = 0;
    api_error = 0; confirm = true; unreadable = NULL; activity = ZMK_ACTIVITY_ACTIVE;
    disconnect_in_api = activity_in_api = activity_in_scan = false;
#if defined(ZBLP_SPLIT_PREEMPT_TEST)
    quiet_ready = inject_quiet = false;
#endif
#if defined(ZBLP_SPLIT_ACTIVITY_TEST)
#if defined(ZBLP_SPLIT_CORE_DYA)
    zmk_activity_set_idle_ms(60000);
#endif
    raise_zmk_position_state_changed((struct zmk_position_state_changed){
        .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL, .position = 0,
        .state = false, .timestamp = k_uptime_get()});
    settle();
#else
    update_activity(activity); settle();
#endif
}
static void after(void *fixture) {
    ARG_UNUSED(fixture);
    struct k_work_sync sync;
    k_work_cancel_delayable_sync(&split_work, &sync);
    for (unsigned i = 0; i < ARRAY_SIZE(connections); i++) {
        zassert_equal(connections[i].references, 0, "Connection reference leaked");
        if (i) { zassert_equal(connections[i].requests, 0, "Host connection was changed"); }
    }
}

#if !defined(ZBLP_SPLIT_ACTIVITY_TEST)
ZTEST(split_idle, test_boot_active_is_quiet) {
    zassert_equal(requests, 0); zassert_false(k_work_delayable_is_pending(&split_work));
}
ZTEST(split_idle, test_idle_uses_30ms_then_stops) {
    update_activity(ZMK_ACTIVITY_IDLE); settle();
    zassert_equal(requests, 1); zassert_equal(last_interval, 24, "Idle must request 30 ms");
    zassert_false(k_work_delayable_is_pending(&split_work));
    k_msleep(1100); zassert_equal(requests, 1);
}
ZTEST(split_idle, test_input_restores_active_interval) {
    update_activity(ZMK_ACTIVITY_IDLE); settle(); update_activity(ZMK_ACTIVITY_ACTIVE);
    k_msleep(520); zassert_equal(requests, 2); zassert_equal(last_interval, 6);
    zassert_false(k_work_delayable_is_pending(&split_work));
}
ZTEST(split_idle, test_old_idle_completion_preserves_latest_active_intent) {
    confirm = false; update_activity(ZMK_ACTIVITY_IDLE); settle();
    update_activity(ZMK_ACTIVITY_ACTIVE); settle(); zassert_equal(requests, 1);
    confirm = true; observed(24,30,400); k_msleep(520);
    zassert_equal(requests, 2); zassert_equal(last_interval, 6);
}
ZTEST(split_idle, test_activity_inside_api_is_not_lost) {
    activity_in_api = true; update_activity(ZMK_ACTIVITY_IDLE); k_msleep(540);
    zassert_equal(requests, 2); zassert_equal(last_interval, 6);
}
ZTEST(split_idle, test_refused_requests_are_bounded) {
    api_error = -EBUSY; update_activity(ZMK_ACTIVITY_IDLE); k_msleep(1600);
    zassert_equal(requests, 3); zassert_false(k_work_delayable_is_pending(&split_work));
    k_msleep(5000); zassert_equal(requests, 3);
}
ZTEST(split_idle, test_accepted_unapplied_requests_settle_then_stop) {
    confirm = false; update_activity(ZMK_ACTIVITY_IDLE); k_msleep(4900);
    zassert_equal(requests, 1); k_msleep(200); zassert_equal(requests, 2);
    k_msleep(5100); zassert_equal(requests, 3); k_msleep(5100);
    zassert_equal(requests, 3); zassert_false(k_work_delayable_is_pending(&split_work));
}
ZTEST(split_idle, test_disconnected_peer_has_no_timer) {
    connections[0].info.state = BT_CONN_STATE_DISCONNECTED; disconnected(&connections[0], 0);
    update_activity(ZMK_ACTIVITY_IDLE); settle(); zassert_equal(requests, 0);
    zassert_false(k_work_delayable_is_pending(&split_work));
}
ZTEST(split_idle, test_reconnect_while_idle_requests_idle_interval) {
    connections[0].info.state = BT_CONN_STATE_DISCONNECTED; disconnected(&connections[0], 0);
    update_activity(ZMK_ACTIVITY_IDLE); settle();
    connections[0].info.state = BT_CONN_STATE_CONNECTED; connected(&connections[0], 0); settle();
    zassert_equal(requests, 1); zassert_equal(last_interval, 24);
}
ZTEST(split_idle, test_disconnect_during_api_drops_old_pending_state) {
    disconnect_in_api = true; update_activity(ZMK_ACTIVITY_IDLE); settle();
    zassert_false(policy.connected); zassert_false(policy.pending);
    zassert_false(k_work_delayable_is_pending(&split_work));
}
ZTEST(split_idle, test_hosts_are_never_changed) {
    connections[0].info.state = BT_CONN_STATE_DISCONNECTED;
    update_activity(ZMK_ACTIVITY_IDLE); settle(); zassert_equal(requests, 0);
}
ZTEST(split_idle, test_other_identity_is_ignored) {
    connections[0].info.id = 1; update_activity(ZMK_ACTIVITY_IDLE); settle();
    zassert_equal(requests, 0);
}
ZTEST(split_idle, test_ambiguous_central_links_are_rejected) {
    connections[6].info.state = BT_CONN_STATE_CONNECTED;
    connections[6].info.role = BT_CONN_ROLE_CENTRAL;
    update_activity(ZMK_ACTIVITY_IDLE); settle(); zassert_equal(requests, 0);
}
ZTEST(split_idle, test_unreadable_peer_is_ignored) {
    unreadable = &connections[0]; update_activity(ZMK_ACTIVITY_IDLE); settle();
    zassert_equal(requests, 0);
}
ZTEST(split_idle, test_stale_scan_cannot_issue_an_idle_request) {
    activity_in_scan = true; update_activity(ZMK_ACTIVITY_IDLE); settle();
    zassert_equal(requests, 0); zassert_false(k_work_delayable_is_pending(&split_work));
}
ZTEST(split_idle, test_sleep_stops_pending_retry) {
    confirm = false; update_activity(ZMK_ACTIVITY_IDLE); settle();
    update_activity(ZMK_ACTIVITY_SLEEP); settle(); k_msleep(6000);
    zassert_equal(requests, 1); zassert_false(k_work_delayable_is_pending(&split_work));
}
ZTEST(split_idle, test_abandoned_sleep_restores_active) {
    update_activity(ZMK_ACTIVITY_IDLE); settle(); update_activity(ZMK_ACTIVITY_SLEEP); settle();
    update_activity(ZMK_ACTIVITY_ACTIVE); k_msleep(520);
    zassert_equal(last_interval, 6); zassert_equal(requests, 2);
}
ZTEST(split_idle, test_latency_is_preserved_when_interval_matches) {
    connections[0].info.le.latency = 0; update_activity(ZMK_ACTIVITY_ACTIVE); settle();
    zassert_equal(requests, 1); zassert_equal(connections[0].info.le.latency, 30);
}
ZTEST(split_idle, test_timeout_is_preserved_when_interval_matches) {
    connections[0].info.le.timeout = 300; update_activity(ZMK_ACTIVITY_ACTIVE); settle();
    zassert_equal(requests, 1); zassert_equal(connections[0].info.le.timeout, 400);
}
ZTEST(split_idle, test_non_le_peer_is_ignored) {
    connections[0].info.type = BT_CONN_TYPE_BR; update_activity(ZMK_ACTIVITY_IDLE); settle();
    zassert_equal(requests, 0);
}
ZTEST(split_idle, test_invalid_activity_event_is_ignored) {
    zassert_equal(activity_changed(NULL), 0); settle(); zassert_equal(requests, 0);
}
ZTEST(split_idle, test_policy_wrap_and_stale_completion) {
    struct zblp_split_policy p = {.active_interval=6,.idle_interval=24,.latency=30,.timeout=400};
    zblp_split_policy_link(&p,true); zblp_split_policy_mode(&p,ZBLP_SPLIT_IDLE);
    zblp_split_policy_observe(&p,(struct zblp_split_parameters){6,30,400});
    struct zblp_split_request request;
    zassert_equal(zblp_split_policy_prepare(&p,UINT32_MAX-99,&request),0);
    zassert_true(request.valid); zblp_split_policy_complete(&p,request,-EBUSY,UINT32_MAX-99);
    zassert_equal(zblp_split_policy_prepare(&p,399,&request),1);
    zassert_equal(zblp_split_policy_prepare(&p,400,&request),0); zassert_true(request.valid);
    struct zblp_split_request old=request; zblp_split_policy_link(&p,false);
    zblp_split_policy_link(&p,true); zblp_split_policy_complete(&p,old,0,401);
    zassert_false(p.pending);
}
#if defined(ZBLP_SPLIT_PREEMPT_TEST)
ZTEST(split_idle, test_quiet_worker_preserves_preempting_activity_wake) {
    inject_quiet = true; update_activity(ZMK_ACTIVITY_ACTIVE);
    zassert_equal(k_sem_take(&producer_done,K_SECONDS(1)),0);
    settle(); zassert_true(quiet_ready); zassert_equal(requests,1,"Quiet wake must survive");
    zassert_equal(last_interval,24);
}
#endif
#endif /* !ZBLP_SPLIT_ACTIVITY_TEST */
ZTEST_SUITE(split_idle,NULL,setup,before,after,NULL);
