/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "link_policy.h"

/* Independent oracle: the fixed demotion delay in milliseconds. It does not follow the product macro. */
#define GRACE_MS 6000U

static void selected_modes(void)
{
    struct zblp_link_policy p = {0};
    const uint32_t times[] = {0, 100, 9999, 10000, 30000, UINT32_MAX, 0};
    /* Independent oracle: one selected slot, or no BLE selection. Traffic and
     * elapsed time are not inputs to the desired mode. */
    for (unsigned t = 0; t < sizeof(times) / sizeof(times[0]); t++) {
        for (uint8_t selected = 0; selected <= 5; selected++) {
            for (uint8_t slot = 0; slot < 5; slot++) {
                assert(zblp_link_fast(&p, slot, slot == selected, times[t]) == (slot == selected));
            }
        }
        assert(zblp_link_next(&p, times[t]) == UINT32_MAX);
    }
    struct zblp_link_policy before = p;
    zblp_link_reset(&p, 255);
    assert(memcmp(&p, &before, sizeof(p)) == 0);
    assert(!zblp_link_fast(&p, 255, true, 0));
    assert(!zblp_link_attempt(&p, 255, true, false, 0));
}

static void retries(void)
{
    struct zblp_link_policy p = {0};
    for (uint32_t t = 0; t < 15000; t++) {
        const bool expected = t == 0 || t == 5000 || t == 10000;
        assert(zblp_link_attempt(&p, 0, false, false, t) == expected);
    }
    assert(zblp_link_next(&p, 15000) == UINT32_MAX);
    assert(zblp_link_attempt(&p, 0, true, false, 15000));
    assert(zblp_link_next(&p, 15000) == 5000);
    assert(!zblp_link_attempt(&p, 0, true, true, 15001));
    assert(zblp_link_next(&p, 15001) == UINT32_MAX);
    assert(!zblp_link_attempt(&p, 0, true, false, 15002));
    assert(zblp_link_next(&p, 15002) == 4998);
    assert(zblp_link_attempt(&p, 0, true, false, 20000));
    assert(zblp_link_attempt(&p, 4, false, false, UINT32_MAX - 499U));
    assert(!zblp_link_attempt(&p, 4, false, false, 4499));
    assert(zblp_link_attempt(&p, 4, false, false, 4500));
    zblp_link_prune(&p, 0, 4501);
    assert(zblp_link_next(&p, 4501) == UINT32_MAX);
    assert(zblp_link_attempt(&p, 4, false, false, 4502));
    zblp_link_reset(&p, 4);
    assert(!p.slot[4].mode_valid);
    zblp_link_reset(&p, 0);
    assert(zblp_link_attempt(&p, 0, false, false, 10000));
    zblp_link_accepted(&p, 0, false, p.slot[0].generation);
    /* The actual link is still fast, but an earlier idle request is pending.
     * A new selected target must supersede that stored request exactly once. */
    assert(!zblp_link_attempt(&p, 0, true, true, 10001));
    assert(!zblp_link_attempt(&p, 0, true, true, 10002));
    assert(zblp_link_next(&p, 10002) == 19998);
    assert(!zblp_link_attempt(&p, 0, true, true, 15000));
    assert(zblp_link_attempt(&p, 0, true, true, 30000));
    zblp_link_accepted(&p, 0, true, p.slot[0].generation);
    zblp_link_observed(&p, 0, true, false);
    assert(!zblp_link_attempt(&p, 0, true, true, 30001));
    assert(zblp_link_next(&p, 30001) == UINT32_MAX);
}

static void mode_change_spacing_regression(void)
{
    struct zblp_link_policy p = {0};
    assert(zblp_link_attempt(&p, 0, false, false, 0));
    zblp_link_accepted(&p, 0, false, p.slot[0].generation);
    /* A new selection must retain a superseding request, without bypassing
     * the same connection's five-second command gate. */
    assert(!zblp_link_attempt(&p, 0, true, true, 1000));
    assert(zblp_link_next(&p, 1000) == 19000);
    /* Independent per-connection request-time oracle. Alternate desired modes
     * every millisecond; even a match must not refill the command gate. */
    uint32_t last_request = 0;
    for (uint32_t t = 1001; t <= 20001; t++) {
        const bool fast = (t % 2U) != 0;
        if (zblp_link_attempt(&p, 0, fast, false, t)) {
            assert(t - last_request >= 5000);
            last_request = t;
        }
    }
    assert(last_request == 20001);
    assert(zblp_link_attempt(&p, 1, true, false, 20002));
    struct zblp_link_policy long_idle = {0};
    assert(zblp_link_attempt(&long_idle, 2, true, false, 0));
    assert(!zblp_link_attempt(&long_idle, 2, true, true, 1));
    assert(zblp_link_attempt(&long_idle, 2, true, false, UINT32_C(2147488648)));
    /* An API refusal does not replace a previously accepted preference. */
    zblp_link_reset(&p, 0);
    assert(zblp_link_attempt(&p, 0, false, false, 0));
    zblp_link_accepted(&p, 0, false, p.slot[0].generation);
    assert(!zblp_link_attempt(&p, 0, true, true, 5000));
    assert(zblp_link_attempt(&p, 0, true, true, 20000));
    assert(!zblp_link_attempt(&p, 0, true, true, 20001));
    assert(zblp_link_next(&p, 20001) == 4999);
    assert(zblp_link_attempt(&p, 0, true, true, 25000));
    /* A completion from a disconnected generation cannot re-arm a slot. */
    const uint32_t generation = p.slot[0].generation;
    zblp_link_reset(&p, 0);
    zblp_link_accepted(&p, 0, false, generation);
    assert(!p.slot[0].preference_valid);
    zblp_link_accepted(&p, 255, false, generation);
}

static void accepted_wait_and_callback_regressions(void)
{
    struct zblp_link_policy p = {0};
    /* Independent elapsed-time oracle: accepted requests get20s, refused
     * attempts get5s, and only a matching callback retires a pending request. */
    assert(zblp_link_attempt(&p, 0, false, false, UINT32_MAX - 9999U));
    zblp_link_accepted(&p, 0, false, p.slot[0].generation);
    for (uint32_t elapsed = 1; elapsed < 20000; elapsed++) {
        const uint32_t now = UINT32_MAX - 9999U + elapsed;
        assert(!zblp_link_attempt(&p, 0, (elapsed % 2U) != 0, false, now));
        assert(zblp_link_next(&p, now) == 20000U - elapsed);
    }
    assert(zblp_link_attempt(&p, 0, true, false, 10000));
    /* Values that are neither the fast nor the idle ones say nothing about the request, before or
     * after the API returns. The idle values do (idle_answer_*): they end the settlement. */
    zblp_link_observed(&p, 0, false, false);
    zblp_link_accepted(&p, 0, true, p.slot[0].generation);
    assert(p.slot[0].preference_valid);
    zblp_link_observed(&p, 0, false, false);
    assert(p.slot[0].preference_valid);
    zblp_link_observed(&p, 0, true, false);
    assert(!p.slot[0].preference_valid);
    assert(!zblp_link_attempt(&p, 0, true, true, 10001));
    assert(zblp_link_next(&p, 10001) == UINT32_MAX);
    /* Confirmation during the API must not become pending again on return. */
    assert(zblp_link_attempt(&p, 1, true, false, 0));
    zblp_link_observed(&p, 1, true, false);
    zblp_link_accepted(&p, 1, true, p.slot[1].generation);
    assert(!p.slot[1].preference_valid);
    assert(!zblp_link_attempt(&p, 1, false, true, 1));
    assert(zblp_link_next(&p, 1) == UINT32_MAX);
    /* A confirmed request cannot force a needless opposite request when
     * the peer's current values already satisfy the new target. */
    zblp_link_observed(&p, 1, false, true);
    assert(!zblp_link_attempt(&p, 1, false, true, 5000));
    zblp_link_observed(&p, 255, true, true);
    zblp_link_reset(&p, 1);
    assert(!p.slot[1].preference_valid && p.slot[1].observation_sequence == 0);
    p.slot[1].observation_sequence = UINT32_MAX;
    assert(zblp_link_attempt(&p, 1, true, false, 0));
    zblp_link_observed(&p, 1, true, false);
    zblp_link_accepted(&p, 1, true, p.slot[1].generation);
    assert(!p.slot[1].preference_valid);
}

static void idle_grace_coalesces_latest_intent(void)
{
    struct zblp_link_policy p = {0};
    assert(!zblp_link_attempt(&p, 0, true, true, UINT32_MAX - 999U));
    assert(!zblp_link_attempt(&p, 0, false, false, UINT32_MAX - 499U));
    assert(zblp_link_next(&p, UINT32_MAX - 499U) == GRACE_MS);
    assert(!zblp_link_attempt(&p, 0, true, true, 0));
    assert(zblp_link_next(&p, 0) == UINT32_MAX);
    assert(!zblp_link_attempt(&p, 0, false, false, 100));
    assert(!zblp_link_attempt(&p, 0, false, false, 100U + GRACE_MS - 1U));
    assert(zblp_link_next(&p, 100U + GRACE_MS - 1U) == 1);
    assert(zblp_link_attempt(&p, 0, false, false, 100U + GRACE_MS));
    /* Returning to fast is not delayed by the idle grace; only an actual
     * previous command's independent5s gate can defer it. */
    assert(!zblp_link_attempt(&p, 0, true, false, 101U + GRACE_MS));
    assert(zblp_link_next(&p, 101U + GRACE_MS) == 4999);
    assert(zblp_link_attempt(&p, 0, true, false, 100U + GRACE_MS + 5000U));
    struct zblp_link_policy independent = {0};
    assert(!zblp_link_attempt(&independent, 1, false, true, 0));
    assert(zblp_link_attempt(&independent, 1, true, false, 1));
}

static void grace_is_six_seconds_from_the_idle_transition(void)
{
    struct zblp_link_policy p = {0};
    const uint32_t start = UINT32_MAX - 2999U; /* the six-second window wraps */
    assert(!zblp_link_attempt(&p, 1, true, true, start - 1000U));
    for (uint32_t elapsed = 0; elapsed < GRACE_MS; elapsed++) {
        assert(!zblp_link_attempt(&p, 1, false, false, start + elapsed));
        assert(zblp_link_next(&p, start + elapsed) == GRACE_MS - elapsed);
    }
    assert(zblp_link_attempt(&p, 1, false, false, start + GRACE_MS));
    assert(p.slot[1].attempts == 1);
}

static void selection_change_restarts_every_pending_demotion(void)
{
    struct zblp_link_policy p = {0};
    const uint32_t t0 = UINT32_MAX - 4999U; /* wraps inside the first window */
    for (uint8_t slot = 0; slot < 2; slot++) {
        assert(!zblp_link_attempt(&p, slot, true, true, t0 - 1000U));
    }
    zblp_link_select(&p, 2, t0 - 500U); /* first selection: nothing is pending yet */
    assert(!zblp_link_attempt(&p, 0, false, false, t0));
    assert(!zblp_link_attempt(&p, 1, false, false, t0));
    assert(zblp_link_next(&p, t0) == GRACE_MS);
    /* A selection change restarts the fixed delay of every pending demotion. */
    zblp_link_select(&p, 3, t0 + 3000U);
    assert(p.slot[0].idle_ms == t0 + 3000U);
    assert(p.slot[1].idle_ms == t0 + 3000U);
    assert(zblp_link_next(&p, t0 + 3000U) == GRACE_MS);
    for (uint32_t elapsed = 3000U; elapsed < 3000U + GRACE_MS; elapsed++) {
        if (elapsed == 5000U) {
            /* The same selection again does not restart anything. */
            zblp_link_select(&p, 3, t0 + elapsed);
            assert(p.slot[0].idle_ms == t0 + 3000U);
        }
        assert(!zblp_link_attempt(&p, 0, false, false, t0 + elapsed));
        assert(zblp_link_next(&p, t0 + elapsed) == 3000U + GRACE_MS - elapsed);
    }
    assert(zblp_link_attempt(&p, 0, false, false, t0 + 3000U + GRACE_MS));
    assert(zblp_link_attempt(&p, 1, false, false, t0 + 3000U + GRACE_MS));
    /* A slot that never had a pending demotion is not touched. */
    const struct zblp_link_slot untouched = {0};
    assert(memcmp(&p.slot[4], &untouched, sizeof(untouched)) == 0);

    /* No BLE selection (USB, or an invalid index) is one selection value. */
    struct zblp_link_policy q = {0};
    const uint32_t u = 1000000U;
    zblp_link_select(&q, 0, u - 20000U);
    assert(!zblp_link_attempt(&q, 0, true, true, u - 1000U));
    zblp_link_select(&q, 1, u);
    assert(!zblp_link_attempt(&q, 0, false, false, u));
    assert(zblp_link_next(&q, u) == GRACE_MS);
    zblp_link_select(&q, 7, u + 2000U); /* out of range: no selection, a change from slot 1 */
    assert(zblp_link_next(&q, u + 2000U) == GRACE_MS);
    zblp_link_select(&q, UINT8_MAX, u + 3000U); /* still no selection */
    assert(zblp_link_next(&q, u + 3000U) == GRACE_MS - 1000U);
    zblp_link_select(&q, 4, u + 7000U);
    assert(zblp_link_next(&q, u + 7000U) == GRACE_MS);
}

static void restart_keeps_budgets_and_later_deadlines(void)
{
    struct zblp_link_policy p = {0};
    assert(zblp_link_attempt(&p, 2, false, false, 0));
    zblp_link_accepted(&p, 2, false, p.slot[2].generation);
    assert(!zblp_link_attempt(&p, 2, true, true, 100));
    assert(!zblp_link_attempt(&p, 2, false, false, 200));
    assert(p.slot[2].idle_gate);
    assert(zblp_link_next(&p, 200) == 19800);
    struct zblp_link_slot before = p.slot[2];
    zblp_link_select(&p, 4, 1000);
    assert(p.slot[2].idle_ms == 1000);
    /* The 20 s settlement of the accepted request is later than the new delay:
     * a restart never pulls a later deadline earlier. */
    assert(zblp_link_next(&p, 1000) == 19000);
    /* Attempts, command gate, accepted preference and its time are unchanged. */
    before.idle_ms = p.slot[2].idle_ms;
    assert(memcmp(&before, &p.slot[2], sizeof(before)) == 0);
}

static void idle_request_widens_after_an_unconfirmed_attempt(void)
{
    struct zblp_link_policy p = {0};
    assert(!zblp_link_idle_wide(&p, 0));
    assert(zblp_link_attempt(&p, 0, false, false, 0)); /* the first idle request is narrow */
    assert(!zblp_link_idle_wide(&p, 0));
    zblp_link_accepted(&p, 0, false, p.slot[0].generation); /* accepted by the API, never applied */
    assert(!zblp_link_attempt(&p, 0, false, false, 19999));
    assert(!zblp_link_idle_wide(&p, 0)); /* waiting does not widen: only a new request does */
    assert(zblp_link_attempt(&p, 0, false, false, 20000)); /* the second request */
    assert(zblp_link_idle_wide(&p, 0));
    assert(!zblp_link_idle_wide(&p, 1)); /* independent per connection */
    assert(zblp_link_attempt(&p, 0, false, false, 25000)); /* the third, still wide */
    assert(zblp_link_idle_wide(&p, 0));
    /* The flag is sticky across modes: a later idle episode starts wide. */
    assert(zblp_link_attempt(&p, 0, true, false, 30000));
    assert(zblp_link_idle_wide(&p, 0));
    assert(!zblp_link_attempt(&p, 0, false, false, 31000));
    assert(zblp_link_idle_wide(&p, 0));
    /* A new connection (reset or prune) starts narrow again. */
    zblp_link_reset(&p, 0);
    assert(!zblp_link_idle_wide(&p, 0));
    assert(zblp_link_attempt(&p, 0, false, false, 40000));
    assert(!zblp_link_idle_wide(&p, 0));
    assert(zblp_link_attempt(&p, 3, false, false, 1000));
    assert(zblp_link_attempt(&p, 3, false, false, 6000)); /* refused attempts count as attempts too */
    assert(zblp_link_idle_wide(&p, 3));
    zblp_link_prune(&p, 1U << 0, 7000);
    assert(!zblp_link_idle_wide(&p, 3));
    /* A confirmed first request never widens: the next episode restarts the count. */
    assert(zblp_link_attempt(&p, 4, false, false, 0));
    zblp_link_accepted(&p, 4, false, p.slot[4].generation);
    zblp_link_observed(&p, 4, false, true);
    assert(!zblp_link_attempt(&p, 4, true, true, 10));
    assert(!zblp_link_attempt(&p, 4, false, false, 20));
    assert(zblp_link_attempt(&p, 4, false, false, 20U + GRACE_MS));
    assert(!zblp_link_idle_wide(&p, 4));
    /* Selected (fast) requests never set it. */
    assert(zblp_link_attempt(&p, 2, true, false, 0));
    assert(zblp_link_attempt(&p, 2, true, false, 5000));
    assert(zblp_link_attempt(&p, 2, true, false, 10000));
    assert(!zblp_link_idle_wide(&p, 2));
    assert(!zblp_link_idle_wide(&p, 5));
    assert(!zblp_link_idle_wide(&p, 255));
}

/* A selected request that the peer answers with the idle values is not pending any more: the 20 s
 * settlement ends and the request is repeated once the five-second command gate allows it.
 * Measured on hardware: Windows repeated its latency 30 link within a millisecond to three seconds of
 * 5 of 12 first promotions and applied nothing until the 20 s retry. Literals are the oracle. */
static void idle_answer_ends_the_settlement_of_a_selected_request(void)
{
    struct zblp_link_policy p = {0};
    assert(zblp_link_attempt(&p, 0, true, false, 1000));
    zblp_link_accepted(&p, 0, true, p.slot[0].generation);
    assert(p.slot[0].preference_valid);
    assert(!zblp_link_attempt(&p, 0, true, false, 2000));
    assert(zblp_link_next(&p, 2000) == 19000);
    /* The idle values arrive after the API returned: the peer answered without applying the request. */
    zblp_link_observed(&p, 0, false, true);
    assert(!p.slot[0].preference_valid);
    assert(zblp_link_next(&p, 2000) == 4000);
    for (uint32_t t = 2001; t < 6000; t++) {
        assert(!zblp_link_attempt(&p, 0, true, false, t));
        assert(zblp_link_next(&p, t) == 6000U - t);
    }
    assert(zblp_link_attempt(&p, 0, true, false, 6000));
    assert(p.slot[0].attempts == 2);
    /* An answer older than the new request does not cancel it. */
    zblp_link_accepted(&p, 0, true, p.slot[0].generation);
    assert(p.slot[0].preference_valid);
    assert(zblp_link_next(&p, 6001) == 19999);
    /* A late answer, after the new request's own gate, allows the repeat at once. */
    zblp_link_observed(&p, 0, false, true);
    assert(!p.slot[0].preference_valid);
    assert(!zblp_link_attempt(&p, 0, true, false, 10999));
    assert(zblp_link_attempt(&p, 0, true, false, 11000));
    assert(p.slot[0].attempts == 3);
}

static void idle_answer_inside_the_api_call_is_not_lost(void)
{
    struct zblp_link_policy p = {0};
    const uint32_t t0 = UINT32_MAX - 2000U; /* the gate wraps */
    assert(zblp_link_attempt(&p, 2, true, false, t0));
    /* The callback runs before the API call returns, as in the kernel fixture. */
    zblp_link_observed(&p, 2, false, true);
    zblp_link_accepted(&p, 2, true, p.slot[2].generation);
    assert(!p.slot[2].preference_valid);
    assert(zblp_link_next(&p, t0) == 5000);
    assert(zblp_link_next(&p, t0 + 4999U) == 1);
    assert(!zblp_link_attempt(&p, 2, true, false, t0 + 4999U));
    assert(zblp_link_attempt(&p, 2, true, false, t0 + 5000U));
    /* The same answer is not read twice: nothing was observed since the new request. */
    zblp_link_accepted(&p, 2, true, p.slot[2].generation);
    assert(p.slot[2].preference_valid);
    /* A disconnected generation cannot be marked answered. */
    const uint32_t generation = p.slot[2].generation;
    zblp_link_reset(&p, 2);
    zblp_link_observed(&p, 2, false, true);
    zblp_link_accepted(&p, 2, true, generation);
    assert(!p.slot[2].preference_valid && !p.slot[2].waiting);
}

static void repeated_idle_answers_stay_within_three_attempts(void)
{
    struct zblp_link_policy p = {0};
    const uint32_t t0 = UINT32_MAX - 7500U; /* the 15 s span wraps */
    for (uint32_t k = 0; k < 3; k++) {
        const uint32_t t = t0 + 5000U * k;
        assert(zblp_link_attempt(&p, 1, true, false, t));
        zblp_link_accepted(&p, 1, true, p.slot[1].generation);
        zblp_link_observed(&p, 1, false, true);
        assert(!p.slot[1].preference_valid);
        assert(p.slot[1].attempts == k + 1U);
        assert(!zblp_link_attempt(&p, 1, true, false, t + 4999U));
    }
    /* The budget is spent: no fourth request and no timer, however long it takes. */
    assert(!zblp_link_attempt(&p, 1, true, false, t0 + 15000U));
    assert(zblp_link_next(&p, t0 + 15000U) == UINT32_MAX);
    assert(!zblp_link_attempt(&p, 1, true, false, t0 + 60000U));
    /* Fast values restore the budget, as for any other request. */
    zblp_link_observed(&p, 1, true, false);
    assert(!zblp_link_attempt(&p, 1, true, true, t0 + 60001U));
    assert(p.slot[1].attempts == 0);
}

static void idle_answers_apply_only_to_pending_selected_requests(void)
{
    struct zblp_link_policy p = {0};
    /* An idle request is not answered by the fast values: only its own values retire it. */
    assert(zblp_link_attempt(&p, 0, false, false, 0));
    zblp_link_accepted(&p, 0, false, p.slot[0].generation);
    zblp_link_observed(&p, 0, true, false);
    assert(p.slot[0].preference_valid);
    assert(!zblp_link_attempt(&p, 0, false, false, 5000));
    assert(zblp_link_next(&p, 5000) == 15000);
    zblp_link_observed(&p, 0, false, true);
    assert(!p.slot[0].preference_valid);
    /* Values that are neither fast nor idle are no answer to a selected request. */
    assert(zblp_link_attempt(&p, 3, true, false, 0));
    zblp_link_accepted(&p, 3, true, p.slot[3].generation);
    zblp_link_observed(&p, 3, false, false);
    assert(p.slot[3].preference_valid);
    assert(zblp_link_next(&p, 1) == 19999);
    /* A request confirmed during the API call leaves nothing to answer later. */
    assert(zblp_link_attempt(&p, 1, true, false, 0));
    zblp_link_observed(&p, 1, true, false);
    zblp_link_accepted(&p, 1, true, p.slot[1].generation);
    zblp_link_observed(&p, 1, false, true);
    assert(!p.slot[1].preference_valid && !p.slot[1].waiting);
    assert(zblp_link_next(&p, 1) == 19999); /* slots 0 and 3 still wait for their settlements */
    zblp_link_observed(&p, 255, false, true);
    /* A stale unconfirmed request whose link has matched since is answered by later idle values:
     * it waits for nothing and the repeat is due once its gate allows. */
    assert(zblp_link_attempt(&p, 2, true, false, 0));
    zblp_link_accepted(&p, 2, true, p.slot[2].generation);
    assert(!zblp_link_attempt(&p, 2, true, true, 25000));
    assert(!p.slot[2].waiting && p.slot[2].preference_valid);
    zblp_link_observed(&p, 2, false, true);
    assert(p.slot[2].waiting && !p.slot[2].preference_valid);
    assert(zblp_link_attempt(&p, 2, true, false, 25001));
    assert(p.slot[2].attempts == 1);
}

/* A host that applies nothing below 15 ms (a Mac applied none of the selected requests of 7..7 and stayed at 12) is
 * asked for the compatible range after two unapplied selected requests. Literals are the oracle: two requests, the
 * second one's 20 s settlement, a fresh budget of three. */
static void selected_request_fails_over_for_a_host_that_applies_nothing(void)
{
    struct zblp_link_policy p = {0};
    assert(!zblp_link_failed_over(&p, 4));
    assert(zblp_link_attempt(&p, 4, true, false, 0)); /* the first request, below 15 ms */
    zblp_link_accepted(&p, 4, true, p.slot[4].generation);
    for (uint32_t t = 0; t <= 20000; t += 1000) { /* one request is not enough, settling or not */
        assert(!zblp_link_fail_over(&p, 4, true, true, false, t));
    }
    assert(zblp_link_attempt(&p, 4, true, false, 20000)); /* the second, after the first one's settlement */
    zblp_link_accepted(&p, 4, true, p.slot[4].generation);
    assert(p.slot[4].attempts == 2);
    for (uint32_t t = 20000; t < 40000; t += 500) { /* the second request is still settling */
        assert(!zblp_link_fail_over(&p, 4, true, true, false, t));
        assert(!zblp_link_failed_over(&p, 4));
    }
    assert(!zblp_link_fail_over(&p, 4, true, true, true, 40000)); /* the link already holds the configured range */
    assert(!zblp_link_fail_over(&p, 4, true, false, false, 40000)); /* the configured range is the compatible one */
    assert(!zblp_link_fail_over(&p, 4, false, true, false, 40000)); /* idle requests are never failed over */
    assert(zblp_link_fail_over(&p, 4, true, true, false, 40000));
    assert(zblp_link_failed_over(&p, 4));
    assert(p.slot[4].attempts == 0 && !p.slot[4].preference_valid && !p.slot[4].waiting);
    assert(zblp_link_next(&p, 40000) == UINT32_MAX);
    assert(zblp_link_fail_over(&p, 4, true, true, true, 40001)); /* sticky: the answer stays true */
    /* The compatible request gets its own budget of three and is issued at once (the command gate was long over). */
    assert(zblp_link_attempt(&p, 4, true, false, 40000));
    assert(zblp_link_attempt(&p, 4, true, false, 45000));
    assert(zblp_link_attempt(&p, 4, true, false, 50000));
    assert(!zblp_link_attempt(&p, 4, true, false, 55000));
    assert(zblp_link_failed_over(&p, 4));
    /* A host that already sits in the compatible range is asked for nothing. */
    struct zblp_link_policy q = {0};
    assert(zblp_link_attempt(&q, 4, true, false, 0));
    assert(zblp_link_attempt(&q, 4, true, false, 5000)); /* an API refusal never settles: the second request after the gate */
    assert(zblp_link_fail_over(&q, 4, true, true, false, 5000));
    assert(!zblp_link_attempt(&q, 4, true, true, 5001));
    assert(q.slot[4].attempts == 0 && !q.slot[4].waiting);
    /* Other connections are independent, and an out-of-range slot is nothing. */
    assert(!zblp_link_failed_over(&p, 0) && !zblp_link_failed_over(&p, 3) && !zblp_link_failed_over(&p, 5));
    /* An out-of-range slot is nothing, even when slot 0 (the slot an index folded by the slot count would hit) is due. */
    struct zblp_link_policy o = {0};
    assert(zblp_link_attempt(&o, 0, true, false, 0));
    assert(zblp_link_attempt(&o, 0, true, false, 5000));
    assert(!zblp_link_fail_over(&o, 255, true, true, false, 5000));
    assert(!zblp_link_fail_over(&o, 5, true, true, false, 5000));
    assert(!zblp_link_failed_over(&o, 0));
    assert(zblp_link_fail_over(&o, 0, true, true, false, 5000));
    assert(!zblp_link_failed_over(&o, 5) && !zblp_link_failed_over(&o, 255) && zblp_link_failed_over(&o, 0));
    /* The state is per connection and sticky across modes; a new connection starts from the configured range. */
    assert(!zblp_link_attempt(&p, 4, false, false, 60000)); /* the demotion delay first */
    assert(zblp_link_attempt(&p, 4, false, false, 60000 + GRACE_MS));
    assert(zblp_link_failed_over(&p, 4));
    zblp_link_prune(&p, 0, 70000);
    assert(!zblp_link_failed_over(&p, 4));
    assert(zblp_link_attempt(&p, 4, true, false, 70000));
    assert(!zblp_link_fail_over(&p, 4, true, true, false, 70001));
    zblp_link_reset(&p, 4);
    assert(!zblp_link_failed_over(&p, 4));
    /* A host that applies the second request (Windows at the retry) is never failed over. */
    struct zblp_link_policy w = {0};
    assert(zblp_link_attempt(&w, 0, true, false, 0));
    zblp_link_accepted(&w, 0, true, w.slot[0].generation);
    zblp_link_observed(&w, 0, false, true); /* answered with the idle values: no settlement */
    assert(!zblp_link_fail_over(&w, 0, true, true, false, 1000));
    assert(zblp_link_attempt(&w, 0, true, false, 5000));
    zblp_link_accepted(&w, 0, true, w.slot[0].generation);
    zblp_link_observed(&w, 0, true, false); /* applied */
    assert(!zblp_link_fail_over(&w, 0, true, true, true, 5100));
    assert(!zblp_link_attempt(&w, 0, true, true, 5101));
    assert(w.slot[0].attempts == 0 && !zblp_link_failed_over(&w, 0));
    /* Requests of an idle episode do not count towards a selected failover. */
    struct zblp_link_policy v = {0};
    assert(zblp_link_attempt(&v, 1, false, false, 0));
    assert(zblp_link_attempt(&v, 1, false, false, 5000));
    assert(!zblp_link_fail_over(&v, 1, true, true, false, 20000));
    assert(!zblp_link_failed_over(&v, 1));
}

static void capacity_preserves_deadlines_and_budget(void)
{
    struct zblp_link_policy p = {0};
    for (uint32_t now = 0; now < 100; now++) {
        assert(!zblp_link_attempt_with_capacity(&p, 1, false, false, now, false));
        assert(p.slot[1].attempts == 0);
        assert(!p.slot[1].gate_valid);
        assert(!p.slot[1].idle_wide);
        assert(zblp_link_next(&p, now) == 0);
    }
    assert(zblp_link_attempt_with_capacity(&p, 1, false, false, 100, true));
    assert(p.slot[1].attempts == 1);
    assert(!zblp_link_attempt_with_capacity(&p, 1, false, false, 5099, false));
    assert(zblp_link_next(&p, 5099) == 1);
    assert(!zblp_link_attempt_with_capacity(&p, 1, false, false, 5100, false));
    assert(p.slot[1].attempts == 1 && !p.slot[1].idle_wide);
    assert(zblp_link_next(&p, 5100) == 0);
    assert(zblp_link_attempt_with_capacity(&p, 1, false, false, 5100, true));
    assert(p.slot[1].attempts == 2 && p.slot[1].idle_wide);
    assert(!zblp_link_attempt_with_capacity(&p, 2, true, true, 5100, false));
    assert(!p.slot[2].waiting && p.slot[2].attempts == 0);
    zblp_link_reset(&p, 1);
    assert(!zblp_link_attempt_with_capacity(&p, 1, true, true, 10000, false));
    assert(!zblp_link_attempt_with_capacity(&p, 1, false, false, 10100, false));
    assert(p.slot[1].idle_ms == 10100);
    assert(zblp_link_next(&p, 10100) == 6000);
    assert(!zblp_link_attempt_with_capacity(&p, 1, false, false, 16100, false));
    assert(zblp_link_next(&p, 16100) == 0 && p.slot[1].attempts == 0);
    assert(zblp_link_attempt_with_capacity(&p, 1, false, false, 16100, true));
    assert(p.slot[1].attempts == 1);
}

int main(void)
{
    capacity_preserves_deadlines_and_budget();
    selected_request_fails_over_for_a_host_that_applies_nothing();
    idle_answer_ends_the_settlement_of_a_selected_request();
    idle_answer_inside_the_api_call_is_not_lost();
    repeated_idle_answers_stay_within_three_attempts();
    idle_answers_apply_only_to_pending_selected_requests();
    grace_is_six_seconds_from_the_idle_transition();
    selection_change_restarts_every_pending_demotion();
    restart_keeps_budgets_and_later_deadlines();
    idle_request_widens_after_an_unconfirmed_attempt();
    accepted_wait_and_callback_regressions();
    idle_grace_coalesces_latest_intent();
    mode_change_spacing_regression();
    selected_modes();
    retries();
    puts("PASS: idle link policy, fixed six-second demotion, selection restart, interval fallback, selected-host failover, early retry after an idle answer and independent attempt oracle and wrap/reset boundaries");
    return 0;
}
