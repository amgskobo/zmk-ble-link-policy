/* SPDX-License-Identifier: MIT
 * Unchanged selected ZMK activity/event sources and actual Zephyr input queues.
 * Right inputs start at the received-position/proxy-input boundary; no radio
 * or host HID is emulated. Bluetooth parameter completion remains controlled.
 */
#include <zmk/events/position_state_changed.h>
#include <zephyr/input/input.h>
#include "main.c"

DEVICE_DEFINE(wake_local_pad, "synthetic_local_pad", NULL, NULL, NULL, NULL,
              POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, NULL);
DEVICE_DEFINE(wake_split_proxy, "synthetic_split_proxy", NULL, NULL, NULL, NULL,
              POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, NULL);

enum origin { LOCAL_KEY, SPLIT_KEY, LOCAL_PAD, SPLIT_PAD };
static unsigned delivered_positions, delivered_inputs;

static int record_position(const zmk_event_t *event) {
    const struct zmk_position_state_changed *position = as_zmk_position_state_changed(event);
    zassert_not_null(position);
    delivered_positions++;
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(wake_position_observer, record_position);
ZMK_SUBSCRIPTION(wake_position_observer, zmk_position_state_changed);

static void record_input(struct input_event *event, void *context) {
    ARG_UNUSED(context);
    zassert_true(event->dev == DEVICE_GET(wake_local_pad) ||
                 event->dev == DEVICE_GET(wake_split_proxy));
    delivered_inputs++;
}
INPUT_CALLBACK_DEFINE(NULL, record_input, NULL);

static void emit(enum origin origin) {
    if (origin == LOCAL_KEY || origin == SPLIT_KEY) {
        /* Split central.c forwards the peripheral index (0 for one configured peer).
         * Local KSCAN uses the explicit LOCAL sentinel. Positions are synthetic
         * representatives of local/received event origins; no board matrix is selected. */
        for (unsigned state = 0; state < 2; state++) {
            zassert_ok(raise_zmk_position_state_changed((struct zmk_position_state_changed){
                .source = origin == LOCAL_KEY ? ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL : 0,
                .position = origin == LOCAL_KEY ? 0 : 1,
                .state = state == 0, .timestamp = k_uptime_get()}));
        }
    } else {
        /* input_split.c reports through the selected proxy device. The global
         * activity callback receives both it and the local input device. */
        const struct device *device = origin == LOCAL_PAD ? DEVICE_GET(wake_local_pad) :
                                                          DEVICE_GET(wake_split_proxy);
        zassert_ok(input_report_rel(device, INPUT_REL_X, 1, true, K_NO_WAIT));
    }
}

static void enter_idle(void) {
    uint32_t start = k_uptime_get_32();
    while (zmk_activity_get_state() != ZMK_ACTIVITY_IDLE || last_interval != 24) {
        zassert_true(k_uptime_get_32() - start < 62000, "Real activity timer never reached IDLE");
        k_msleep(20);
    }
    zassert_equal(requests, 1);
    zassert_true(k_uptime_get_32() - last_request_ms < 100,
                 "This scenario must exercise a recent idle request");
}

static void require_fast_restore(enum origin origin) {
    const uint32_t input_ms = k_uptime_get_32();
    emit(origin);
    k_msleep(10);
    zassert_equal(zmk_activity_get_state(), ZMK_ACTIVITY_ACTIVE);
    zassert_equal(requests, 2, "ACTIVE must bypass the idle request gate");
    zassert_equal(last_interval, 6);
    zassert_equal(connections[0].info.le.interval, 6);
    zassert_true(last_request_ms - input_ms <= 10,
                 "Controlled kernel wake must issue the request within10ms");
    zassert_false(k_work_delayable_is_pending(&split_work));
    printk("WAKE v=1 origin=%u input_ms=%u request_ms=%u delay_ms=%u interval=%u\n",
           origin, input_ms, last_request_ms, last_request_ms - input_ms, last_interval);
}

ZTEST(split_idle, test_local_key_immediate_wake) {
    enter_idle(); require_fast_restore(LOCAL_KEY);
}
ZTEST(split_idle, test_split_key_immediate_wake) {
    enter_idle(); require_fast_restore(SPLIT_KEY);
}
ZTEST(split_idle, test_local_pad_immediate_wake) {
    enter_idle(); require_fast_restore(LOCAL_PAD);
}
ZTEST(split_idle, test_split_pad_immediate_wake) {
    enter_idle(); require_fast_restore(SPLIT_PAD);
}
ZTEST(split_idle, test_both_sides_from_settled_idle) {
    for (enum origin source = LOCAL_KEY; source <= SPLIT_PAD; source++) {
        if (source != LOCAL_KEY) { before(NULL); }
        enter_idle(); k_msleep(600); require_fast_restore(source);
    }
}
ZTEST(split_idle, test_pending_idle_completion_restores_latest_input) {
    confirm = false; enter_idle();
    zassert_equal(connections[0].info.le.interval, 6);
    unsigned initial = delivered_positions;
    emit(LOCAL_KEY); emit(SPLIT_KEY); k_msleep(10);
    zassert_equal(zmk_activity_get_state(), ZMK_ACTIVITY_ACTIVE);
    zassert_equal(delivered_positions, initial + 4);
    zassert_equal(requests, 1, "Do not replace an accepted procedure before its callback");
    const uint32_t applied_ms = k_uptime_get_32();
    confirm = true; observed(24,30,400); k_msleep(10);
    zassert_equal(requests, 2); zassert_equal(last_interval, 6);
    zassert_true(last_request_ms - applied_ms <= 10);
    zassert_false(k_work_delayable_is_pending(&split_work));
}
ZTEST(split_idle, test_alternating_sides_keeps_active_without_chatter) {
    enter_idle();
    unsigned positions_before = delivered_positions, inputs_before = delivered_inputs;
    for (unsigned i = 0; i < 32; i++) { emit((enum origin)(i % 4)); k_msleep(1); }
    k_msleep(10);
    zassert_equal(zmk_activity_get_state(), ZMK_ACTIVITY_ACTIVE);
    zassert_equal(delivered_positions, positions_before + 32);
    zassert_equal(delivered_inputs, inputs_before + 16);
    zassert_equal(requests, 2); zassert_equal(last_interval, 6);
    k_msleep(600); zassert_equal(requests, 2);
    zassert_false(k_work_delayable_is_pending(&split_work));
}
ZTEST(split_idle, test_three_idle_active_cycles_preserve_fast_restore) {
    /* Exercise the public saved-timeout API, with the real1s activity timer. */
#if defined(ZBLP_SPLIT_CORE_DYA)
    zassert_true(zmk_activity_set_idle_ms(1));
#endif
    for (unsigned cycle = 0; cycle < 3; cycle++) {
        if (cycle) {
            requests = 0; last_interval = 6;
        }
        enter_idle(); require_fast_restore(cycle % 2 ? SPLIT_KEY : LOCAL_KEY);
    }
#if defined(ZBLP_SPLIT_CORE_DYA)
    zassert_true(zmk_activity_set_idle_ms(60000));
#endif
}
ZTEST(split_idle, test_refused_active_restore_retains_retry_gate) {
    enter_idle(); api_error = -EBUSY;
    uint32_t input_ms = k_uptime_get_32(); emit(SPLIT_KEY); k_msleep(10);
    zassert_equal(requests, 2); zassert_equal(last_interval, 6);
    zassert_true(last_request_ms - input_ms <= 10);
    zassert_equal(connections[0].info.le.interval, 24);
    k_msleep(470); zassert_equal(requests, 2);
    api_error = 0; k_msleep(40);
    zassert_equal(requests, 3); zassert_equal(connections[0].info.le.interval, 6);
    zassert_false(k_work_delayable_is_pending(&split_work));
}
