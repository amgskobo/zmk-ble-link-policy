/* SPDX-License-Identifier: GPL-3.0-only */
#include "zblp_timeout.h"
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/logging/log.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include "link_policy.h"

LOG_MODULE_REGISTER(zblp_idle_links, CONFIG_ZMK_BLE_LINK_POLICY_LOG_LEVEL);

#define IDLE_LATENCY 30U
/* An idle request asks for the preferred 12..12 first. A peer that did not apply it (a Mac
 * applied only 3 of 6 such requests but 138 of 141 requests of 12..24) is asked again with the
 * wider maximum 12..24, and its connection keeps asking so until it disconnects
 * (zblp_link_idle_wide). Windows promoted in 2.8..5.6 s while its idle link stayed at 15 ms and
 * in 5.8..12 s once idle requests allowed 30 ms, so the wide maximum is a fallback, not the
 * default. A peer's actual idle interval up to this maximum is accepted either way. */
#define IDLE_MAX_INTERVAL MAX(CONFIG_BT_PERIPHERAL_PREF_MAX_INT, 24U)
BUILD_ASSERT(CONFIG_BT_PERIPHERAL_PREF_TIMEOUT * 8U >
             2U * IDLE_MAX_INTERVAL * (IDLE_LATENCY + 1U));
/* Idle requests never ask for less than the compatible minimum (15 ms, the minimum of Apple's accessory guidelines):
 * an inactive host has no use for a fast link, and a Mac or phone applied none of the idle requests that started below it.
 * With the selected-host range at 12..12 or above all of this is the same as before. */
#define IDLE_MIN_INTERVAL MAX(CONFIG_BT_PERIPHERAL_PREF_MIN_INT, ZBLP_LINK_COMPAT_MIN)
#define IDLE_FIRST_MAX_INTERVAL MAX(CONFIG_BT_PERIPHERAL_PREF_MAX_INT, IDLE_MIN_INTERVAL)
BUILD_ASSERT(IDLE_MIN_INTERVAL <= IDLE_FIRST_MAX_INTERVAL && IDLE_FIRST_MAX_INTERVAL <= IDLE_MAX_INTERVAL);
/* A selected range that starts below the compatible minimum may be refused by a host (an Apple host applied none of
 * the selected requests of 7..7 and stayed at 12 units). Such a connection is failed over, after
 * ZBLP_LINK_FAILOVER_ATTEMPTS unapplied requests, to the compatible range until it disconnects (zblp_link_fail_over). */
#define FAST_CAN_FAIL_OVER (CONFIG_BT_PERIPHERAL_PREF_MIN_INT < ZBLP_LINK_COMPAT_MIN)
#define COMPAT_FAST_MAX_INTERVAL MAX(CONFIG_BT_PERIPHERAL_PREF_MAX_INT, ZBLP_LINK_COMPAT_MIN)

static struct zblp_link_policy policy;
static struct k_spinlock policy_lock;
static uint64_t wake_generation;
static void reconcile(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(link_work, reconcile);

#if defined(CONFIG_ZMK_BLE_LINK_POLICY_DIAGNOSTICS)
static uint32_t work_sequence;

static uint32_t elapsed_us(uint32_t start, uint32_t end, bool *valid)
{
    const uint32_t delta = end - start;
    const uint64_t us = k_cyc_to_us_floor64(delta);
    *valid = delta < UINT32_C(0x80000000) && us <= UINT32_MAX;
    return *valid ? (uint32_t)us : 0U;
}
#endif

static void wake_locked(void)
{
    wake_generation++;
    (void)k_work_reschedule(&link_work, K_NO_WAIT);
}

static void wake(void)
{
    k_spinlock_key_t key = k_spin_lock(&policy_lock);
    wake_locked();
    k_spin_unlock(&policy_lock, key);
}

struct scan {
    struct zmk_endpoint_instance selected;
    uint8_t connected;
    bool active_pass;
    bool issued;
    uint64_t generation;
#if defined(CONFIG_ZMK_BLE_LINK_POLICY_DIAGNOSTICS)
    uint32_t api_us;
    uint32_t api_max_us;
    uint32_t api_calls;
    uint32_t invalid;
#endif
};

static void reconcile_one(struct bt_conn *conn, void *data)
{
    struct scan *scan = data;
    /* foreach holds a LE reference. Classify the pass before the full info
     * query; preserve eligibility checks before recording a connected slot. */
    const int slot = zmk_ble_profile_index(bt_conn_get_dst(conn));
    if (slot < 0 || slot >= (int)ZBLP_LINK_SLOTS) {
        return;
    }
    const bool selected = scan->selected.transport == ZMK_TRANSPORT_BLE &&
                          scan->selected.ble.profile_index == slot;
    if (selected != scan->active_pass) { return; }
    struct bt_conn_info info;
    if (bt_conn_get_info(conn, &info) != 0 || info.type != BT_CONN_TYPE_LE ||
        info.state != BT_CONN_STATE_CONNECTED || info.role != BT_CONN_ROLE_PERIPHERAL ||
        info.id != BT_ID_DEFAULT) {
        return;
    }
    scan->connected |= (uint8_t)(1U << slot);
    k_spinlock_key_t key = k_spin_lock(&policy_lock);
    if (scan->generation != wake_generation) {
        k_spin_unlock(&policy_lock, key);
        return;
    }
    const uint32_t now = k_uptime_get_32();
    const bool fast = zblp_link_fast(&policy, (uint8_t)slot, selected, now);
    const uint16_t latency = fast ? CONFIG_BT_PERIPHERAL_PREF_LATENCY : IDLE_LATENCY;
    const bool was_failed_over = zblp_link_failed_over(&policy, (uint8_t)slot);
    const bool configured = fast && info.le.latency == CONFIG_BT_PERIPHERAL_PREF_LATENCY &&
                            info.le.interval >= CONFIG_BT_PERIPHERAL_PREF_MIN_INT &&
                            info.le.interval <= CONFIG_BT_PERIPHERAL_PREF_MAX_INT;
    /* Decide the failover before the request is built: from then on this selected host is asked for the compatible range. */
    const bool failed_over = fast && zblp_link_fail_over(&policy, (uint8_t)slot, fast, FAST_CAN_FAIL_OVER, configured, now);
    const uint16_t min_interval = fast ? (failed_over ? ZBLP_LINK_COMPAT_MIN : CONFIG_BT_PERIPHERAL_PREF_MIN_INT)
                                       : IDLE_MIN_INTERVAL;
    const uint16_t max_interval = fast ? (failed_over ? COMPAT_FAST_MAX_INTERVAL : CONFIG_BT_PERIPHERAL_PREF_MAX_INT)
                                       : IDLE_MAX_INTERVAL;
    const bool matched = info.le.latency == latency &&
                         info.le.interval >= min_interval &&
                         info.le.interval <= max_interval;
    const bool request = zblp_link_attempt_with_capacity(&policy, (uint8_t)slot, fast,
                                                        matched, now, !scan->issued);
    const uint32_t generation = policy.slot[slot].generation;
    /* Read after the attempt: the second idle request of an episode is the first wide one. */
    const bool wide = !fast && zblp_link_idle_wide(&policy, (uint8_t)slot);
    k_spin_unlock(&policy_lock, key);
    if (failed_over && !was_failed_over) {
        LOG_INF("link failover slot=%d interval=%u..%u (the host did not apply %u..%u)", slot,
                (unsigned)ZBLP_LINK_COMPAT_MIN, (unsigned)COMPAT_FAST_MAX_INTERVAL,
                (unsigned)CONFIG_BT_PERIPHERAL_PREF_MIN_INT, (unsigned)CONFIG_BT_PERIPHERAL_PREF_MAX_INT);
    }
    if (!request) {
        return;
    }
    scan->issued = true;
    const struct bt_le_conn_param param = {
        .interval_min = min_interval,
        .interval_max = fast ? max_interval : (wide ? IDLE_MAX_INTERVAL : IDLE_FIRST_MAX_INTERVAL),
        .latency = latency,
        .timeout = CONFIG_BT_PERIPHERAL_PREF_TIMEOUT,
    };
#if defined(CONFIG_ZMK_BLE_LINK_POLICY_DIAGNOSTICS)
    const uint32_t api_start = k_cycle_get_32();
#endif
    const int err = bt_conn_le_param_update(conn, &param);
#if defined(CONFIG_ZMK_BLE_LINK_POLICY_DIAGNOSTICS)
    bool valid;
    const uint32_t api_us = elapsed_us(api_start, k_cycle_get_32(), &valid);
    scan->api_calls++;
    if (!valid || api_us > UINT32_MAX - scan->api_us) {
        scan->invalid++;
    } else {
        scan->api_us += api_us;
        scan->api_max_us = MAX(scan->api_max_us, api_us);
    }
#endif
    if (err == 0) {
        key = k_spin_lock(&policy_lock);
        zblp_link_accepted(&policy, (uint8_t)slot, fast, generation);
        k_spin_unlock(&policy_lock, key);
    }
    LOG_INF("link preference slot=%d active=%u latency=%u interval=%u..%u err=%d observed_interval=%u observed_latency=%u observed_timeout=%u",
            slot, (unsigned)fast, (unsigned)latency, (unsigned)param.interval_min,
            (unsigned)param.interval_max, err, (unsigned)info.le.interval,
            (unsigned)info.le.latency, (unsigned)info.le.timeout);
}

static void reconcile(struct k_work *work)
{
    ARG_UNUSED(work);
#if defined(CONFIG_ZMK_BLE_LINK_POLICY_DIAGNOSTICS)
    const uint32_t work_start = k_cycle_get_32();
    const uint32_t sequence = ++work_sequence;
    uint32_t pending_ms = UINT32_MAX;
#endif
    k_spinlock_key_t key = k_spin_lock(&policy_lock);
    const uint64_t generation = wake_generation;
    k_spin_unlock(&policy_lock, key);
    struct scan scan = {.selected = zmk_endpoint_get_selected(), .active_pass = true,
                        .generation = generation};
    /* foreach owns references only for this pass; disconnected objects cannot
     * shadow a newer connected object and the split central link is excluded. */
    bt_conn_foreach(BT_CONN_TYPE_LE, reconcile_one, &scan);
    key = k_spin_lock(&policy_lock);
    const bool current = generation == wake_generation;
    if (current) {
        /* A new selection restarts the fixed demotion delay of every previous host. */
        zblp_link_select(&policy,
                        scan.selected.transport == ZMK_TRANSPORT_BLE ? scan.selected.ble.profile_index
                                                                      : UINT8_MAX,
                        k_uptime_get_32());
    }
    k_spin_unlock(&policy_lock, key);
    /* Queue the selected host before idle hosts. A newer endpoint/callback
     * wake must reconcile again before issuing the old pass's idle requests. */
    if (current) {
        scan.active_pass = false;
        bt_conn_foreach(BT_CONN_TYPE_LE, reconcile_one, &scan);
    }
    key = k_spin_lock(&policy_lock);
    if (generation == wake_generation) {
        const uint32_t now = k_uptime_get_32();
        zblp_link_prune(&policy, scan.connected, now);
        const uint32_t delay = zblp_link_next(&policy, now);
#if defined(CONFIG_ZMK_BLE_LINK_POLICY_DIAGNOSTICS)
        pending_ms = delay;
#endif
        if (delay == UINT32_MAX) {
            (void)k_work_cancel_delayable(&link_work);
        } else {
            (void)k_work_reschedule(&link_work, K_TICKS(zblp_timeout_ticks((uint32_t)(delay), CONFIG_SYS_CLOCK_TICKS_PER_SEC)));
        }
    }
#if defined(CONFIG_ZMK_BLE_LINK_POLICY_DIAGNOSTICS)
    const bool stale = generation != wake_generation;
#endif
    k_spin_unlock(&policy_lock, key);
#if defined(CONFIG_ZMK_BLE_LINK_POLICY_DIAGNOSTICS)
    if (scan.api_calls != 0U) {
        bool valid;
        const uint32_t work_us = elapsed_us(work_start, k_cycle_get_32(), &valid);
        LOG_INF("linkwork v=1 seq=%u work_us=%u api_calls=%u api_us=%u api_max_us=%u invalid=%u pending_ms=%u active_slot=%u host_mask=%u stale=%u",
                (unsigned)sequence, (unsigned)work_us, (unsigned)scan.api_calls,
                (unsigned)scan.api_us, (unsigned)scan.api_max_us,
                (unsigned)(scan.invalid + !valid), (unsigned)pending_ms,
                scan.selected.transport == ZMK_TRANSPORT_BLE ?
                    (unsigned)scan.selected.ble.profile_index : (unsigned)UINT8_MAX,
                (unsigned)scan.connected, (unsigned)stale);
    }
#endif
}

/* Lifecycle events outside the host policy must not rescan every connection
 * or reset a host slot that happens to share the address of another role. */
static int lifecycle_slot(struct bt_conn *conn, bool connected_only)
{
    const int slot = zmk_ble_profile_index(bt_conn_get_dst(conn));
    if (slot < 0 || slot >= (int)ZBLP_LINK_SLOTS) {
        return -1;
    }
    struct bt_conn_info info;
    if (bt_conn_get_info(conn, &info) != 0 || info.type != BT_CONN_TYPE_LE ||
        info.role != BT_CONN_ROLE_PERIPHERAL || info.id != BT_ID_DEFAULT ||
        (connected_only && info.state != BT_CONN_STATE_CONNECTED)) {
        return -1;
    }
    return slot;
}

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err == 0 && lifecycle_slot(conn, true) >= 0) {
        wake();
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    ARG_UNUSED(reason);
    const int slot = lifecycle_slot(conn, false);
    if (slot < 0) {
        return;
    }
    k_spinlock_key_t key = k_spin_lock(&policy_lock);
    zblp_link_reset(&policy, (uint8_t)slot);
    wake_locked();
    k_spin_unlock(&policy_lock, key);
}

static void parameters(struct bt_conn *conn, uint16_t interval, uint16_t latency, uint16_t timeout)
{
    struct bt_conn_info info;
    if (bt_conn_get_info(conn, &info) == 0 && info.type == BT_CONN_TYPE_LE &&
        info.role == BT_CONN_ROLE_PERIPHERAL && info.id == BT_ID_DEFAULT &&
        info.state == BT_CONN_STATE_CONNECTED) {
        const int slot = zmk_ble_profile_index(bt_conn_get_dst(conn));
        if (slot >= 0 && slot < (int)ZBLP_LINK_SLOTS) {
            k_spinlock_key_t key = k_spin_lock(&policy_lock);
            /* A failed-over selected host is judged by the compatible range, as it was asked for it. */
            const bool failed_over = zblp_link_failed_over(&policy, (uint8_t)slot);
            const uint16_t fast_min = failed_over ? ZBLP_LINK_COMPAT_MIN : CONFIG_BT_PERIPHERAL_PREF_MIN_INT;
            const uint16_t fast_max = failed_over ? COMPAT_FAST_MAX_INTERVAL : CONFIG_BT_PERIPHERAL_PREF_MAX_INT;
            zblp_link_observed(&policy, (uint8_t)slot,
                interval >= fast_min && interval <= fast_max && latency == CONFIG_BT_PERIPHERAL_PREF_LATENCY,
                interval >= IDLE_MIN_INTERVAL && interval <= IDLE_MAX_INTERVAL && latency == IDLE_LATENCY);
            /* Publish observation and scan invalidation together; an old
             * worker must not issue from facts read before this callback. */
            wake_locked();
            k_spin_unlock(&policy_lock, key);
            LOG_INF("link parameters slot=%d interval=%u latency=%u timeout=%u",
                    slot, (unsigned)interval, (unsigned)latency, (unsigned)timeout);
            return;
        }
    }
}

static void security(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
    ARG_UNUSED(level);
    ARG_UNUSED(err);
    if (lifecycle_slot(conn, true) >= 0) {
        wake();
    }
}

BT_CONN_CB_DEFINE(zbm_idle_link_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
    .le_param_updated = parameters,
    .security_changed = security,
};

static int endpoint_changed(const zmk_event_t *event)
{
    ARG_UNUSED(event);
    wake();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(zblp_idle_links, endpoint_changed);
ZMK_SUBSCRIPTION(zblp_idle_links, zmk_endpoint_changed);
ZMK_SUBSCRIPTION(zblp_idle_links, zmk_ble_active_profile_changed);

static int init(void)
{
    wake();
    return 0;
}

SYS_INIT(init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
