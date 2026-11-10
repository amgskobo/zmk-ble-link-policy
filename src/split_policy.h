/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define ZBLP_SPLIT_REQUEST_GAP_MS 500U
#define ZBLP_SPLIT_SETTLE_MS 5000U
#define ZBLP_SPLIT_REQUEST_LIMIT 3U
#define ZBLP_SPLIT_NO_TIMER UINT32_MAX

enum zblp_split_mode { ZBLP_SPLIT_ACTIVE, ZBLP_SPLIT_IDLE, ZBLP_SPLIT_SLEEP };

struct zblp_split_parameters {
    uint16_t interval, latency, timeout;
};

struct zblp_split_request {
    bool valid;
    uint32_t epoch;
    struct zblp_split_parameters parameters;
};

struct zblp_split_policy {
    uint16_t active_interval, idle_interval, latency, timeout;
    enum zblp_split_mode mode;
    bool connected, pending, gated;
    uint8_t attempts;
    uint32_t epoch, requested_at, accepted_at;
    struct zblp_split_parameters observed, pending_parameters;
};

static inline bool zblp_split_parameters_equal(struct zblp_split_parameters a,
                                                struct zblp_split_parameters b) {
    return a.interval == b.interval && a.latency == b.latency && a.timeout == b.timeout;
}

static inline void zblp_split_policy_mode(struct zblp_split_policy *p,
                                           enum zblp_split_mode mode) {
    if (p->mode != mode) {
        p->mode = mode;
        p->attempts = 0;
        /* Keep an accepted old procedure: its later application must not
         * overwrite the latest intent without a corrective reconciliation. */
    }
}

static inline void zblp_split_policy_link(struct zblp_split_policy *p, bool connected) {
    p->epoch++;
    p->connected = connected;
    p->pending = p->gated = false;
    p->attempts = 0;
    p->observed = (struct zblp_split_parameters){0};
}

static inline void zblp_split_policy_observe(struct zblp_split_policy *p,
                                              struct zblp_split_parameters actual) {
    p->observed = actual;
    if (p->pending && zblp_split_parameters_equal(actual, p->pending_parameters)) {
        p->pending = false;
    }
}

/* A finite timer exists only for a request gate or an accepted procedure.
 * All uint32 times are elapsed-time comparisons, including across wrap. */
static inline uint32_t zblp_split_policy_prepare(struct zblp_split_policy *p, uint32_t now,
                                                  struct zblp_split_request *request) {
    *request = (struct zblp_split_request){0};
    if (!p->connected || p->mode == ZBLP_SPLIT_SLEEP) {
        return ZBLP_SPLIT_NO_TIMER;
    }
    if (p->pending) {
        uint32_t elapsed = now - p->accepted_at;
        if (elapsed < ZBLP_SPLIT_SETTLE_MS) {
            return ZBLP_SPLIT_SETTLE_MS - elapsed;
        }
        p->pending = false;
    }
    const struct zblp_split_parameters target = {
        p->mode == ZBLP_SPLIT_IDLE ? p->idle_interval : p->active_interval,
        p->latency, p->timeout};
    if (zblp_split_parameters_equal(p->observed, target)) {
        p->attempts = 0;
        return ZBLP_SPLIT_NO_TIMER;
    }
    if (p->attempts >= ZBLP_SPLIT_REQUEST_LIMIT) {
        return ZBLP_SPLIT_NO_TIMER;
    }
    uint32_t elapsed = now - p->requested_at;
    /* Restoring responsiveness gets one immediate attempt. Idle requests and
     * subsequent refused attempts retain the gate, so activity flapping cannot
     * create an unbounded request loop. An accepted old procedure still waits
     * for its parameter callback above before its replacement is requested. */
    const bool first_active_request = p->mode == ZBLP_SPLIT_ACTIVE && p->attempts == 0;
    if (p->gated && elapsed < ZBLP_SPLIT_REQUEST_GAP_MS && !first_active_request) {
        return ZBLP_SPLIT_REQUEST_GAP_MS - elapsed;
    }
    p->attempts++;
    p->gated = true;
    p->requested_at = now;
    *request = (struct zblp_split_request){true, p->epoch, target};
    return 0;
}

static inline void zblp_split_policy_complete(struct zblp_split_policy *p,
                                               struct zblp_split_request request,
                                               int error, uint32_t now) {
    if (!request.valid || !p->connected || p->epoch != request.epoch) {
        return;
    }
    /* A parameter callback can run before the synchronous API returns. */
    if (error == 0 && !zblp_split_parameters_equal(p->observed, request.parameters)) {
        p->pending = true;
        p->pending_parameters = request.parameters;
        p->accepted_at = now;
    }
}
