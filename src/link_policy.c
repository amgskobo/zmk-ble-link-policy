/* SPDX-License-Identifier: GPL-3.0-only */
#include <limits.h>
#include <string.h>
#include "link_policy.h"

void zblp_link_reset(struct zblp_link_policy *p, uint8_t slot)
{
    if (slot < ZBLP_LINK_SLOTS) {
        const uint32_t generation = p->slot[slot].generation + 1U;
        memset(&p->slot[slot], 0, sizeof(p->slot[slot]));
        p->slot[slot].generation = generation;
    }
}

void zblp_link_prune(struct zblp_link_policy *p, uint8_t connected, uint32_t now)
{
    (void)now;
    for (uint8_t i = 0; i < ZBLP_LINK_SLOTS; i++) {
        if ((connected & (1U << i)) == 0U) {
            zblp_link_reset(p, i);
        }
    }
}

bool zblp_link_fast(struct zblp_link_policy *p, uint8_t slot, bool selected, uint32_t now)
{
    if (slot >= ZBLP_LINK_SLOTS) {
        return false;
    }
    (void)p;
    (void)now;
    return selected;
}

static void mode(struct zblp_link_slot *s, bool fast, uint32_t now)
{
    if (!s->mode_valid || s->fast != fast) {
        s->idle_gate = s->mode_valid && s->fast && !fast;
        s->idle_ms = now;
        s->mode_valid = true;
        s->fast = fast;
        s->attempts = 0;
    }
}

void zblp_link_select(struct zblp_link_policy *p, uint8_t selected, uint32_t now)
{
    /* Any invalid index, and USB, is the single value "no BLE selection". */
    const uint8_t slot = selected < ZBLP_LINK_SLOTS ? selected : UINT8_MAX;
    if (p->selected.valid && p->selected.slot == slot) {
        return;
    }
    p->selected.valid = true;
    p->selected.slot = slot;
    for (uint8_t i = 0; i < ZBLP_LINK_SLOTS; i++) {
        struct zblp_link_slot *s = &p->slot[i];
        if (!s->idle_gate) {
            continue;
        }
        /* Restart only the delay. Attempts, command gate and the accepted preference are
         * untouched, and a later settlement or gate deadline is never pulled earlier. */
        s->idle_ms = now;
        if (s->waiting) {
            const uint32_t due = now + ZBLP_LINK_IDLE_GRACE_MS;
            if ((int32_t)(due - s->retry_ms) > 0) {
                s->retry_ms = due;
            }
        }
    }
}

bool zblp_link_idle_wide(const struct zblp_link_policy *p, uint8_t slot)
{
    return slot < ZBLP_LINK_SLOTS && p->slot[slot].idle_wide;
}

bool zblp_link_failed_over(const struct zblp_link_policy *p, uint8_t slot)
{
    return slot < ZBLP_LINK_SLOTS && p->slot[slot].fail_over;
}

bool zblp_link_fail_over(struct zblp_link_policy *p, uint8_t slot, bool fast, bool can, bool matched, uint32_t now)
{
    if (slot >= ZBLP_LINK_SLOTS) {
        return false;
    }
    struct zblp_link_slot *s = &p->slot[slot];
    if (s->fail_over || !fast || !can || matched || !s->mode_valid || !s->fast ||
        s->attempts < ZBLP_LINK_FAILOVER_ATTEMPTS) {
        return s->fail_over;
    }
    /* The last request is still settling: the peer may yet apply it. */
    const bool settling = s->preference_valid && (uint32_t)(now - s->accepted_ms) < ZBLP_LINK_SETTLE_MS;
    if (settling) {
        return false;
    }
    s->fail_over = true;
    s->attempts = 0;
    s->preference_valid = false;
    s->waiting = false;
    return true;
}

bool zblp_link_attempt_with_capacity(struct zblp_link_policy *p, uint8_t slot, bool fast,
                                   bool matched, uint32_t now, bool capacity)
{
    if (slot >= ZBLP_LINK_SLOTS) {
        return false;
    }
    struct zblp_link_slot *s = &p->slot[slot];
    /* An unconfirmed old preference may still be stored or queued by Zephyr. */
    const bool replace_pending = s->preference_valid && s->preference_fast != fast;
    mode(s, fast, now);
    /* API acceptance is not procedure completion. Keep only the latest intent
     * while the peer settles instead of enqueuing another request every5s.
     * This bounded timeout permits recovery, not cancellation of stack work. */
    if (s->preference_valid && (uint32_t)(now - s->accepted_ms) < ZBLP_LINK_SETTLE_MS) {
        s->waiting = true;
        s->retry_ms = s->accepted_ms + ZBLP_LINK_SETTLE_MS;
        return false;
    }
    if (matched && !replace_pending) {
        s->attempts = 0;
        s->waiting = false;
        return false;
    }
    if (s->attempts >= ZBLP_LINK_ATTEMPTS) {
        s->waiting = false;
        return false;
    }
    uint32_t delay = 0U;
    if (s->idle_gate) {
        const uint32_t elapsed = now - s->idle_ms;
        if (elapsed < ZBLP_LINK_IDLE_GRACE_MS) {
            delay = ZBLP_LINK_IDLE_GRACE_MS - elapsed;
        } else {
            s->idle_gate = false;
        }
    }
    /* Returning to the selected host cancels an unsent idle transition. The
     * command gate remains independent of both grace and callback deadlines. */
    if (s->gate_valid && (uint32_t)(now - s->requested_ms) < ZBLP_LINK_RETRY_MS) {
        const uint32_t gate = ZBLP_LINK_RETRY_MS - (uint32_t)(now - s->requested_ms);
        if (gate > delay) { delay = gate; }
    }
    if (delay != 0U) {
        s->waiting = true;
        s->retry_ms = now + delay;
        return false;
    }
    if (!capacity) {
        s->waiting = true;
        s->retry_ms = now;
        return false;
    }
    /* A second idle request of one episode means the peer did not apply the first, so
     * the connection asks for the wider interval maximum from now on. The count is of
     * requests, not of causes: an API refusal counts like a request the peer ignored. */
    if (!fast && s->attempts != 0U) {
        s->idle_wide = true;
    }
    s->attempts++;
    s->gate_valid = true;
    s->requested_ms = now;
    s->request_observation = s->observation_sequence;
    s->waiting = s->attempts < ZBLP_LINK_ATTEMPTS;
    s->retry_ms = now + ZBLP_LINK_RETRY_MS;
    return true;
}

bool zblp_link_attempt(struct zblp_link_policy *p, uint8_t slot, bool fast, bool matched, uint32_t now)
{
    return zblp_link_attempt_with_capacity(p, slot, fast, matched, now, true);
}

void zblp_link_accepted(struct zblp_link_policy *p, uint8_t slot, bool fast, uint32_t generation)
{
    if (slot < ZBLP_LINK_SLOTS && p->slot[slot].generation == generation) {
        struct zblp_link_slot *s = &p->slot[slot];
        /* A callback may run before the synchronous API returns. Do not
         * resurrect a request already confirmed during that call. */
        const bool confirmed = s->observation_sequence != s->request_observation &&
                               (fast ? s->observed_fast : s->observed_idle);
        /* A selected request the peer answered with the idle values during that call is over too. */
        const bool answered = fast && !confirmed && s->observed_idle &&
                              s->observation_sequence != s->request_observation;
        s->preference_valid = !confirmed && !answered;
        s->preference_fast = fast;
        s->accepted_ms = s->requested_ms;
        s->waiting = !confirmed;
        if (answered) {
            s->retry_ms = s->requested_ms + ZBLP_LINK_RETRY_MS;
        } else if (!confirmed) {
            s->retry_ms = s->accepted_ms + ZBLP_LINK_SETTLE_MS;
        }
    }
}

void zblp_link_observed(struct zblp_link_policy *p, uint8_t slot, bool fast, bool idle)
{
    if (slot < ZBLP_LINK_SLOTS) {
        struct zblp_link_slot *s = &p->slot[slot];
        s->observation_sequence++;
        s->observed_fast = fast;
        s->observed_idle = idle;
        if (s->preference_valid) {
            const bool confirmed = s->preference_fast ? fast : idle;
            /* Measured on hardware: Windows repeated its idle link within a millisecond to three seconds
             * of a selected request and applied nothing until the retry 20 s later. The idle values after
             * a selected request mean the peer answered without applying it: the settlement ends and the
             * request may be repeated once its command gate (and attempt budget) allows. */
            const bool answered = !confirmed && s->preference_fast && idle;
            if (confirmed || answered) {
                s->preference_valid = false;
            }
            if (answered) {
                s->waiting = true;
                s->retry_ms = s->requested_ms + ZBLP_LINK_RETRY_MS;
            }
        }
    }
}

uint32_t zblp_link_next(const struct zblp_link_policy *p, uint32_t now)
{
    uint32_t next = UINT32_MAX;
    for (uint8_t i = 0; i < ZBLP_LINK_SLOTS; i++) {
        const struct zblp_link_slot *s = &p->slot[i];
        if (s->waiting) {
            const uint32_t delay = (int32_t)(now - s->retry_ms) >= 0 ? 0U : s->retry_ms - now;
            if (delay < next) {
                next = delay;
            }
        }
    }
    return next;
}
