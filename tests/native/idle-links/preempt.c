/* SPDX-License-Identifier: GPL-3.0-only */
/* Preserve the complete default fixture under a preemptible workqueue. */
#include "main.c"

static void endpoint_event_thread(void *a, void *b, void *c) {
  ARG_UNUSED(a);
  ARG_UNUSED(b);
  ARG_UNUSED(c);
  for (;;) {
    if (k_sem_take(&endpoint_event_ready, K_FOREVER) != 0)
      continue;
    selected.ble.profile_index = 1;
    endpoint_event_active = true;
    endpoint_busy_at_wake = k_work_busy_get(&link_work.work);
    endpoint_changed(NULL);
    endpoint_event_active = false;
    k_sem_give(&endpoint_event_done);
  }
}
K_THREAD_DEFINE(endpoint_event_tid, 2048, endpoint_event_thread, NULL, NULL,
                NULL, 5, 0, 0);

ZTEST(idle_links, test_endpoint_after_quiet_unlock_is_admitted) {
  zassert_equal(CONFIG_SYSTEM_WORKQUEUE_PRIORITY, 10);
  for (unsigned i = 1; i < 5; ++i)
    connections[i].info.le.latency = 30;
  inject_quiet_wake = true;
  wake();
  zassert_equal(k_sem_take(&endpoint_event_done, K_MSEC(100)), 0);
  settle();
  zassert_true(quiet_observed);
  zassert_true(endpoint_busy_at_wake & K_WORK_RUNNING);
  zassert_false(endpoint_busy_at_wake & K_WORK_CANCELING);
  zassert_true(endpoint_schedule_result >= 0);
  zassert_equal(selected.ble.profile_index, 1);
  zassert_equal(policy.selected.slot, 1);
  zassert_equal(connections[1].requests, 1);
  zassert_equal(connections[1].info.le.latency, 0);
  zassert_equal(total, 1);
  /* No replayed external event is required; the command gate also prevents
   * extra work from charging a second attempt in the same episode. */
  k_msleep(1000);
  zassert_equal(connections[1].requests, 1);
  zassert_equal(total, 1);
}
