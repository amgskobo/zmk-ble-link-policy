/* SPDX-License-Identifier: MIT
 * Optional single-peer split policy; host links have independent state.
 */
#include <zephyr/bluetooth/conn.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zmk/activity.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>

#include "split_policy.h"

LOG_MODULE_REGISTER(zblp_split_links, CONFIG_ZMK_BLE_LINK_POLICY_LOG_LEVEL);

BUILD_ASSERT(CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS == 1,
             "Split policy requires exactly one outgoing split peer");
BUILD_ASSERT(CONFIG_ZMK_BLE_SPLIT_POLICY_IDLE_INTERVAL > CONFIG_ZMK_SPLIT_BLE_PREF_INT,
             "Idle split interval must exceed the active preference");
BUILD_ASSERT(CONFIG_ZMK_SPLIT_BLE_PREF_TIMEOUT * 8U >
                 2U * CONFIG_ZMK_BLE_SPLIT_POLICY_IDLE_INTERVAL * (CONFIG_ZMK_SPLIT_BLE_PREF_LATENCY + 1U),
             "Idle split interval violates the BLE supervision timeout bound");

static struct zblp_split_policy policy = {
    .active_interval = CONFIG_ZMK_SPLIT_BLE_PREF_INT,
    .idle_interval = CONFIG_ZMK_BLE_SPLIT_POLICY_IDLE_INTERVAL,
    .latency = CONFIG_ZMK_SPLIT_BLE_PREF_LATENCY,
    .timeout = CONFIG_ZMK_SPLIT_BLE_PREF_TIMEOUT,
};
static struct k_spinlock policy_lock;
static uint64_t wake_generation;
static uint8_t peer_index = UINT8_MAX;
static void reconcile(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(split_work, reconcile);

static void wake_locked(void) {
    wake_generation++;
    (void)k_work_reschedule(&split_work, K_NO_WAIT);
}

static bool split_connection(struct bt_conn *conn, struct bt_conn_info *info) {
    return bt_conn_get_info(conn, info) == 0 && info->type == BT_CONN_TYPE_LE &&
           info->role == BT_CONN_ROLE_CENTRAL && info->id == BT_ID_DEFAULT;
}

struct split_scan {
    struct bt_conn *conn;
    struct bt_conn_info info;
    unsigned count;
};

static void collect_split(struct bt_conn *conn, void *data) {
    struct split_scan *scan = data;
    struct bt_conn_info info;
    if (!split_connection(conn, &info) || info.state != BT_CONN_STATE_CONNECTED) {
        return;
    }
    scan->count++;
    if (scan->count == 1) {
        scan->conn = bt_conn_ref(conn);
        scan->info = info;
    }
}

static void reconcile(struct k_work *work) {
    ARG_UNUSED(work);
    k_spinlock_key_t key = k_spin_lock(&policy_lock);
    const uint64_t generation = wake_generation;
    k_spin_unlock(&policy_lock, key);

    struct split_scan scan = {0};
    bt_conn_foreach(BT_CONN_TYPE_LE, collect_split, &scan);

    key = k_spin_lock(&policy_lock);
    if (generation != wake_generation) {
        k_spin_unlock(&policy_lock, key);
        if (scan.conn) {
            bt_conn_unref(scan.conn);
        }
        return;
    }
    /* This policy supports one outgoing central-role split link. Reject ambiguous
     * topologies instead of changing another central connection's parameters. */
    if (scan.count != 1) {
        if (policy.connected) {
            zblp_split_policy_link(&policy, false);
        }
        peer_index = UINT8_MAX;
        k_spin_unlock(&policy_lock, key);
        if (scan.conn) {
            bt_conn_unref(scan.conn);
        }
        return;
    }
    const uint8_t index = bt_conn_index(scan.conn);
    if (!policy.connected || peer_index != index) {
        zblp_split_policy_link(&policy, true);
        peer_index = index;
    }
    zblp_split_policy_observe(&policy, (struct zblp_split_parameters){
        scan.info.le.interval, scan.info.le.latency, scan.info.le.timeout});
    struct zblp_split_request request;
    const uint32_t delay = zblp_split_policy_prepare(&policy, k_uptime_get_32(), &request);
    if (!request.valid) {
        if (delay != ZBLP_SPLIT_NO_TIMER) {
            (void)k_work_reschedule(&split_work, K_MSEC(delay));
        }
        /* Do not cancel a quiet running worker: a newer wake may already
         * have been queued by a preempting event after this lock is released. */
        k_spin_unlock(&policy_lock, key);
        bt_conn_unref(scan.conn);
        return;
    }
    k_spin_unlock(&policy_lock, key);

    const struct bt_le_conn_param parameters = {
        .interval_min = request.parameters.interval,
        .interval_max = request.parameters.interval,
        .latency = request.parameters.latency,
        .timeout = request.parameters.timeout,
    };
    const int error = bt_conn_le_param_update(scan.conn, &parameters);
    bt_conn_unref(scan.conn);

    key = k_spin_lock(&policy_lock);
    zblp_split_policy_complete(&policy, request, error, k_uptime_get_32());
    /* Refresh actual parameters and the latest activity on a separate pass;
     * one invocation makes at most one potentially blocking Bluetooth call. */
    wake_locked();
    k_spin_unlock(&policy_lock, key);
    LOG_INF("split interval request=%u latency=%u timeout=%u err=%d",
            request.parameters.interval, request.parameters.latency,
            request.parameters.timeout, error);
}

static void connection_changed(struct bt_conn *conn) {
    struct bt_conn_info info;
    if (!split_connection(conn, &info)) {
        return;
    }
    k_spinlock_key_t key = k_spin_lock(&policy_lock);
    zblp_split_policy_link(&policy, false);
    peer_index = UINT8_MAX;
    wake_locked();
    k_spin_unlock(&policy_lock, key);
}

static void connected(struct bt_conn *conn, uint8_t error) {
    if (error == 0) {
        connection_changed(conn);
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason) {
    ARG_UNUSED(reason);
    connection_changed(conn);
}

static void parameters_changed(struct bt_conn *conn, uint16_t interval,
                               uint16_t latency, uint16_t timeout) {
    struct bt_conn_info info;
    if (!split_connection(conn, &info) || info.state != BT_CONN_STATE_CONNECTED) {
        return;
    }
    k_spinlock_key_t key = k_spin_lock(&policy_lock);
    if (policy.connected && peer_index == bt_conn_index(conn)) {
        zblp_split_policy_observe(&policy,
                                    (struct zblp_split_parameters){interval, latency, timeout});
    }
    wake_locked();
    k_spin_unlock(&policy_lock, key);
    LOG_INF("split interval observed=%u latency=%u timeout=%u", interval, latency, timeout);
}

BT_CONN_CB_DEFINE(zblp_split_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
    .le_param_updated = parameters_changed,
};

static void update_activity(enum zmk_activity_state state) {
    k_spinlock_key_t key = k_spin_lock(&policy_lock);
    zblp_split_policy_mode(&policy,
        state == ZMK_ACTIVITY_IDLE ? ZBLP_SPLIT_IDLE :
        state == ZMK_ACTIVITY_SLEEP ? ZBLP_SPLIT_SLEEP : ZBLP_SPLIT_ACTIVE);
    wake_locked();
    k_spin_unlock(&policy_lock, key);
}

static int activity_changed(const zmk_event_t *event) {
    const struct zmk_activity_state_changed *activity = as_zmk_activity_state_changed(event);
    if (activity) {
        update_activity(activity->state);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(zblp_split_activity, activity_changed);
ZMK_SUBSCRIPTION(zblp_split_activity, zmk_activity_state_changed);

static int split_idle_init(void) {
    update_activity(zmk_activity_get_state());
    return 0;
}

SYS_INIT(split_idle_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
