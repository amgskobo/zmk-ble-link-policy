/* SPDX-License-Identifier: MIT */
#pragma once
#include <zmk/activity.h>
#include <zmk/event_manager.h>
struct zmk_activity_state_changed { enum zmk_activity_state state; };
static inline const struct zmk_activity_state_changed *
as_zmk_activity_state_changed(const zmk_event_t *event) {
    return (const struct zmk_activity_state_changed *)event;
}
