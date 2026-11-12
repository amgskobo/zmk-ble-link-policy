/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZBLP_LINK_POLICY_H_
#define ZBLP_LINK_POLICY_H_

#include <stdbool.h>
#include <stdint.h>

#define ZBLP_LINK_SLOTS 5U
#define ZBLP_LINK_RETRY_MS 5000U
#define ZBLP_LINK_SETTLE_MS 20000U
/* A host that stopped being selected is demoted this long after the last selection change,
 * whatever the new host's state. Promotion of the selected host is never delayed by it. */
#define ZBLP_LINK_IDLE_GRACE_MS 6000U
#define ZBLP_LINK_ATTEMPTS 3U
/* The smallest connection interval (1.25 ms units, 12 = 15 ms) asked of a host that did not apply a faster one:
 * the minimum Apple's accessory guidelines allow, and the lower bound of every idle request. */
#define ZBLP_LINK_COMPAT_MIN 12U
/* Selected-host requests below ZBLP_LINK_COMPAT_MIN that the peer left unapplied before the connection is failed over. */
#define ZBLP_LINK_FAILOVER_ATTEMPTS 2U

struct zblp_link_slot {
    /* All accesses share the service's policy lock. Keep each independent
     * boolean without spending a byte per flag in every host slot. */
    bool mode_valid : 1;
    bool fast : 1;
    bool waiting : 1;
    bool gate_valid : 1;
    bool preference_valid : 1;
    bool preference_fast : 1;
    bool idle_gate : 1;
    bool observed_fast : 1;
    bool observed_idle : 1;
    bool idle_wide : 1;
    bool fail_over : 1;
    uint8_t attempts;
    uint32_t generation;
    uint32_t retry_ms;
    uint32_t requested_ms;
    uint32_t accepted_ms;
    uint32_t idle_ms;
    uint32_t observation_sequence;
    uint32_t request_observation;
};

struct zblp_link_policy {
    struct zblp_link_slot slot[ZBLP_LINK_SLOTS];
    struct {
        bool valid;
        uint8_t slot; /* UINT8_MAX: no BLE selection */
    } selected;
};

void zblp_link_reset(struct zblp_link_policy *p, uint8_t slot);
/* Drop absent links before calculating bounded request retries. */
void zblp_link_prune(struct zblp_link_policy *p, uint8_t connected, uint32_t now);
bool zblp_link_fast(struct zblp_link_policy *p, uint8_t slot, bool selected, uint32_t now);
bool zblp_link_attempt(struct zblp_link_policy *p, uint8_t slot, bool fast, bool matched, uint32_t now);
/* Reconcile mode/deadlines without charging a command when capacity is false.
 * A ready deferred command waits until now, so the next worker can issue it.
 * Existing settlement, grace, command gate and attempt bounds still apply. */
bool zblp_link_attempt_with_capacity(struct zblp_link_policy *p, uint8_t slot, bool fast,
                                   bool matched, uint32_t now, bool capacity);
/* The selected BLE slot (any other value, and USB, means none). A change restarts the
 * demotion delay of every pending idle transition; the same value does nothing. Attempts,
 * command gates and accepted preferences are untouched. */
void zblp_link_select(struct zblp_link_policy *p, uint8_t selected, uint32_t now);
/* True once a second idle request was needed in one idle episode of this connection: the
 * interval maximum of its idle requests is then the wide one. Cleared when it disconnects. */
bool zblp_link_idle_wide(const struct zblp_link_policy *p, uint8_t slot);
/* Fail over a selected host that does not apply a request below ZBLP_LINK_COMPAT_MIN (an Apple host takes nothing under 15 ms).
 * `can` is true when the configured selected range starts below the compatible minimum, `matched` when the link already holds
 * the configured range. Once ZBLP_LINK_FAILOVER_ATTEMPTS selected requests were issued, the last one is no longer settling and
 * the link still does not match, the connection is failed over: its selected requests use the compatible range from then on,
 * with a fresh attempt budget, until it disconnects. Idle requests are never affected. Returns the state of the connection
 * after the call (the same value as zblp_link_failed_over). */
bool zblp_link_fail_over(struct zblp_link_policy *p, uint8_t slot, bool fast, bool can, bool matched, uint32_t now);
bool zblp_link_failed_over(const struct zblp_link_policy *p, uint8_t slot);
/* API acceptance records a preference, never confirmation of link parameters. */
void zblp_link_accepted(struct zblp_link_policy *p, uint8_t slot, bool fast, uint32_t generation);
/* Actual callbacks retire an unconfirmed preference; unrelated values do not. The idle values
 * after an accepted selected (fast) request, whether they arrive during the API call or later, are
 * the peer's answer without applying it: that request stops waiting for its settlement and may be
 * repeated after the command gate, within the attempt budget. An idle request is retired only by
 * its own values. */
void zblp_link_observed(struct zblp_link_policy *p, uint8_t slot, bool fast, bool idle);
/* UINT32_MAX means no timer is needed. */
uint32_t zblp_link_next(const struct zblp_link_policy *p, uint32_t now);

#endif
