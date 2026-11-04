/* SPDX-License-Identifier: GPL-3.0-only */
/* Real kernel/workqueue; Bluetooth and ZMK endpoints are recorded stand-ins.
 * Unchanged stock/DYA linked builds separately validate the actual headers. */
#include "link_policy.h"
#include <errno.h>
#include <string.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zmk/endpoints.h>

struct bt_conn {
  bt_addr_le_t dst;
  struct bt_conn_info info;
  unsigned requests;
  uint16_t pending_latency;
  uint16_t last_requested_latency;
  uint16_t requested_min;
  uint16_t requested_max;
};
static struct bt_conn connections[9];
static struct zmk_endpoint_instance selected;
static int request_error;
static bool confirm_request;
static unsigned echo_idle;
static unsigned ignore_requests; /* a peer that applies nothing until it has
                                    been asked this many times */
static bool inject_selection;
static bool inject_info_selection;
static bool inject_after_scan;
static bool inject_disconnect;
static bool check_disconnected_return;
static uint16_t accepted_interval;
static unsigned total;
static uint8_t request_order[64];
static unsigned request_scan[64];
static unsigned scan_serial;
static unsigned info_queries;
static const struct bt_conn *info_error_conn;
static bool probe_enabled;
static bool probe_reselect;
static unsigned probe_count;
static unsigned probe_samples[16];

static void probe_handler(struct k_work *work);
static K_WORK_DEFINE(probe_work, probe_handler);

#if defined(ZBLP_PREEMPT_TEST)
static bool inject_quiet_wake;
static bool quiet_observed;
static bool endpoint_event_active;
static int endpoint_schedule_result;
static unsigned endpoint_busy_at_wake;
K_SEM_DEFINE(endpoint_event_ready, 0, 1);
K_SEM_DEFINE(endpoint_event_done, 0, 1);
static int observed_reschedule(struct k_work_delayable *work,
                               k_timeout_t delay);
static uint32_t observed_next(const struct zblp_link_policy *state,
                              uint32_t now);
static void observed_unlock(struct k_spinlock *lock, k_spinlock_key_t key);
/* Instrument the real API boundary only in this fixture. Queue, timer,
 * cancellation and spinlock results all come from the selected kernel. */
#define k_work_reschedule observed_reschedule
#define zblp_link_next observed_next
#define k_spin_unlock observed_unlock
#endif
static void reconcile(struct k_work *work);
#include ZBLP_LINK_SOURCE
#if defined(ZBLP_PREEMPT_TEST)
#undef k_work_reschedule
#undef zblp_link_next
#undef k_spin_unlock
static int observed_reschedule(struct k_work_delayable *work,
                               k_timeout_t delay) {
  const int result = k_work_reschedule(work, delay);
  if (endpoint_event_active)
    endpoint_schedule_result = result;
  return result;
}
static uint32_t observed_next(const struct zblp_link_policy *state,
                              uint32_t now) {
  const uint32_t result = zblp_link_next(state, now);
  if (inject_quiet_wake && result == UINT32_MAX &&
      k_current_get() == &k_sys_work_q.thread)
    quiet_observed = true;
  return result;
}
static void observed_unlock(struct k_spinlock *lock, k_spinlock_key_t key) {
  k_spin_unlock(lock, key);
  if (inject_quiet_wake && quiet_observed && lock == &policy_lock &&
      k_current_get() == &k_sys_work_q.thread) {
    inject_quiet_wake = false;
    /* A real higher-priority endpoint producer runs after the real unlock,
     * while the work handler is still running. */
    k_sem_give(&endpoint_event_ready);
  }
}
#endif

static void probe_handler(struct k_work *work) {
  ARG_UNUSED(work);
  zassert_true(probe_count < ARRAY_SIZE(probe_samples));
  probe_samples[probe_count++] = total;
  if (probe_reselect) {
    probe_reselect = false;
    selected.ble.profile_index = 3;
    endpoint_changed(NULL);
  }
}

int zmk_ble_profile_index(const bt_addr_le_t *addr) { return addr->a.val[0]; }
struct zmk_endpoint_instance zmk_endpoint_get_selected(void) {
  return selected;
}
const bt_addr_le_t *bt_conn_get_dst(const struct bt_conn *conn) {
  return &conn->dst;
}
int bt_conn_get_info(const struct bt_conn *conn, struct bt_conn_info *info) {
  info_queries++;
  *info = conn->info;
  if (inject_info_selection && conn == &connections[4]) {
    inject_info_selection = false;
    selected.ble.profile_index = 3;
    endpoint_changed(NULL);
  }
  if (conn == info_error_conn)
    return -EIO;
  return 0;
}
void bt_conn_foreach(enum bt_conn_type type,
                     void (*func)(struct bt_conn *, void *), void *context) {
  zassert_equal(type, BT_CONN_TYPE_LE);
  scan_serial++;
  for (unsigned i = 0; i < ARRAY_SIZE(connections); i++) {
    func(&connections[i], context);
  }
  /* Observe the old API return before the worker's later prune or another
   * scan could hide a transient resurrection of the disconnected slot. */
  if (check_disconnected_return) {
    zassert_false(policy.slot[1].preference_valid);
    check_disconnected_return = false;
  }
  if (inject_after_scan) {
    inject_after_scan = false;
    selected.ble.profile_index = 1;
    endpoint_changed(NULL);
  }
}
int bt_conn_le_param_update(struct bt_conn *conn,
                            const struct bt_le_conn_param *param) {
  zassert_equal(k_current_get(), &k_sys_work_q.thread);
  zassert_true(param->interval_min == 12 ||
               param->interval_min == CONFIG_BT_PERIPHERAL_PREF_MIN_INT);
  zassert_true(param->interval_max >= param->interval_min &&
               param->interval_max <= 24);
  zassert_equal(param->timeout, 400);
  if (total < ARRAY_SIZE(request_order)) {
    request_order[total] = conn->dst.a.val[0];
    request_scan[total] = scan_serial;
  }
  total++;
  if (probe_enabled) {
    (void)k_work_submit(&probe_work);
  }
  conn->requests++;
  conn->last_requested_latency = param->latency;
  conn->requested_min = param->interval_min;
  conn->requested_max = param->interval_max;
  if (request_error == 0) {
    conn->pending_latency = param->latency;
  }
  if (inject_disconnect) {
    inject_disconnect = false;
    conn->info.state = BT_CONN_STATE_DISCONNECTED;
    disconnected(conn, 0);
    check_disconnected_return = true;
    return 0;
  }
  if (inject_selection) {
    inject_selection = false;
    selected.ble.profile_index = 1;
    endpoint_changed(NULL);
  }
  if (request_error == 0 && echo_idle != 0 && param->latency == 0) {
    /* The peer answers a selected request with the idle values it already has,
     * inside the API call. */
    echo_idle--;
    parameters(conn, conn->info.le.interval, conn->info.le.latency, 400);
    return request_error;
  }
  if (request_error == 0 && confirm_request &&
      conn->requests > ignore_requests) {
    conn->info.le.latency = param->latency;
    conn->info.le.interval =
        accepted_interval ? accepted_interval : param->interval_min;
    parameters(conn, conn->info.le.interval, param->latency, 400);
  }
  return request_error;
}

static void settle(void) { k_msleep(5); }
static void before(void *fixture) {
  ARG_UNUSED(fixture);
  struct k_work_sync sync;
  k_work_cancel_delayable_sync(&link_work, &sync);
  k_work_cancel_sync(&probe_work, &sync);
#if defined(ZBLP_PREEMPT_TEST)
  inject_quiet_wake = quiet_observed = endpoint_event_active = false;
  endpoint_schedule_result = -ENOSYS;
  endpoint_busy_at_wake = 0;
  k_sem_reset(&endpoint_event_ready);
  k_sem_reset(&endpoint_event_done);
#endif
  memset(&policy, 0, sizeof(policy));
  memset(connections, 0, sizeof(connections));
  request_error = 0;
  confirm_request = true;
  echo_idle = 0;
  ignore_requests = 0;
  inject_selection = false;
  inject_info_selection = false;
  inject_after_scan = false;
  inject_disconnect = false;
  check_disconnected_return = false;
  accepted_interval = 0;
  total = 0;
  scan_serial = 0;
  info_queries = 0;
  info_error_conn = NULL;
  probe_enabled = false;
  probe_reselect = false;
  probe_count = 0;
  memset(probe_samples, 0, sizeof(probe_samples));
  selected = (struct zmk_endpoint_instance){.transport = ZMK_TRANSPORT_BLE,
                                            .ble.profile_index = 0};
  for (unsigned i = 0; i < ARRAY_SIZE(connections); i++) {
    connections[i].dst.a.val[0] = i;
    connections[i].info =
        (struct bt_conn_info){.type = BT_CONN_TYPE_LE,
                              .role = BT_CONN_ROLE_PERIPHERAL,
                              .id = BT_ID_DEFAULT,
                              .state = BT_CONN_STATE_CONNECTED,
                              .le.interval = 12,
                              .le.latency = 0};
  }
  connections[5].info.role = BT_CONN_ROLE_CENTRAL;
  connections[5].dst.a.val[0] =
      2; /* A split address must not count as a host. */
  connections[6].dst.a.val[0] = 255; /* Unknown guest. */
  connections[7].info.id = 1;
  connections[7].dst.a.val[0] = 3;
  connections[8].info.state = BT_CONN_STATE_DISCONNECTED;
  connections[8].dst.a.val[0] = 4;
}
