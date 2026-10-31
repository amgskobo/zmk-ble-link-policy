#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compiled negative controls; require runtime failures after successful builds."""
import argparse
import json
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
out = parser.parse_args().output
out.mkdir(parents=True, exist_ok=False)
source = (root / 'src/link_policy.c').read_text()
faults = {
    'capacity-ignored': ('if (!capacity) {', 'if (false && !capacity) {'),
    'deferred-charges-attempt': ('if (!capacity) {', 'if (!capacity) { s->attempts++;'),
    'deferred-no-wake': ('if (!capacity) {\n        s->waiting = true;', 'if (!capacity) {\n        s->waiting = false;'),
    'deferred-sets-wide': ('if (!capacity) {', 'if (!capacity) { s->idle_wide = true;'),
    'unselected-promoted': ('return selected;', '(void)selected; return true;'),
    'selected-loses-priority': ('return selected;', 'return !selected;'),
    'unbounded-retry': ('s->attempts >= ZBLP_LINK_ATTEMPTS', 's->attempts > ZBLP_LINK_ATTEMPTS'),
    'early-retry': ('(uint32_t)(now - s->requested_ms) < ZBLP_LINK_RETRY_MS', '(uint32_t)(now - s->requested_ms) < ZBLP_LINK_RETRY_MS - 1U'),
    'absent-keeps-timer': ('zblp_link_reset(p, i);', '(void)p; (void)i;'),
    'mode-has-no-budget': ('if (!s->mode_valid || s->fast != fast)', 'if (!s->mode_valid)'),
    'old-pending-preference': ('if (matched && !replace_pending)', 'if (matched || replace_pending)'),
    'mode-bypasses-gate': ('s->fast = fast;', 's->fast = fast; s->gate_valid = false;'),
    'match-erases-gate': ('if (matched && !replace_pending) {', 'if (matched && !replace_pending) { s->gate_valid = false;'),
    'old-generation-accepted': ('p->slot[slot].generation == generation', '(p->slot[slot].generation == generation || true)'),
    'accepted-settle-ignored': ('if (s->preference_valid && (uint32_t)', 'if (false && s->preference_valid && (uint32_t)'),
    'confirmation-keeps-preference': ('            if (confirmed || answered) {\n                s->preference_valid = false;', '            if (confirmed || answered) {\n                s->preference_valid = true;'),
    'inline-confirmation-forgotten': ('const bool confirmed = s->observation_sequence != s->request_observation &&', 'const bool confirmed = false && s->observation_sequence != s->request_observation &&'),
    'unrelated-confirmation': ('s->preference_fast ? fast : idle', 'fast || idle'),
    'idle-grace-removed': ('if (s->idle_gate) {', 'if (false && s->idle_gate) {'),
    'refusal-extends-settlement': ('s->requested_ms = now;', 's->requested_ms = now; s->accepted_ms = now;'),
    'grace-delays-selected': ('s->idle_gate = s->mode_valid && s->fast && !fast;', 's->idle_gate = s->mode_valid && !s->fast && fast;'),
    'select-does-nothing': ('if (p->selected.valid && p->selected.slot == slot) {', 'if (true) {'),
    'select-restarts-on-same-selection': ('if (p->selected.valid && p->selected.slot == slot) {', 'if (false) {'),
    'select-out-of-range-is-a-slot': ('const uint8_t slot = selected < ZBLP_LINK_SLOTS ? selected : UINT8_MAX;', 'const uint8_t slot = selected;'),
    'restart-touches-slots-without-pending-demotion': ('if (!s->idle_gate) {', 'if (false) {'),
    'restart-forgets-idle-time': ('        s->idle_ms = now;\n        if (s->waiting) {', '        if (s->waiting) {'),
    'restart-pulls-deadline-earlier': ('if ((int32_t)(due - s->retry_ms) > 0) {', 'if (true) {'),
    'restart-spends-attempt': ('        s->idle_ms = now;\n        if (s->waiting) {', '        s->idle_ms = now;\n        s->attempts++;\n        if (s->waiting) {'),
    'wide-on-first-attempt': ('if (!fast && s->attempts != 0U) {', 'if (!fast) {'),
    'wide-never-set': ('if (!fast && s->attempts != 0U) {', 'if (false) {'),
    'wide-set-by-selected-attempts': ('if (!fast && s->attempts != 0U) {', 'if (s->attempts != 0U) {'),
    'wide-lost-on-mode-change': ('        s->fast = fast;\n        s->attempts = 0;', '        s->fast = fast;\n        s->attempts = 0;\n        s->idle_wide = false;'),
    'wide-survives-disconnect': ('memset(&p->slot[slot], 0, sizeof(p->slot[slot]));', '{ const bool w = p->slot[slot].idle_wide; memset(&p->slot[slot], 0, sizeof(p->slot[slot])); p->slot[slot].idle_wide = w; }'),
    'wide-reads-wrong-slot': ('return slot < ZBLP_LINK_SLOTS && p->slot[slot].idle_wide;', 'return slot < ZBLP_LINK_SLOTS && p->slot[0].idle_wide;'),
    'answer-ignored-after-return': ('const bool answered = !confirmed && s->preference_fast && idle;', 'const bool answered = false;'),
    'answer-lost-inside-api-call': ('const bool answered = fast && !confirmed && s->observed_idle &&', 'const bool answered = false && fast && !confirmed && s->observed_idle &&'),
    'answer-applies-to-idle-requests': ('const bool answered = !confirmed && s->preference_fast && idle;', 'const bool answered = !confirmed && (s->preference_fast ? idle : fast);'),
    'answer-by-unrelated-values': ('const bool answered = !confirmed && s->preference_fast && idle;', 'const bool answered = !confirmed && s->preference_fast && !fast;'),
    'answer-keeps-settlement': ('            if (confirmed || answered) {', '            if (confirmed) {'),
    'answer-retry-without-gate': ('                s->retry_ms = s->requested_ms + ZBLP_LINK_RETRY_MS;\n            }', '                s->retry_ms = s->requested_ms;\n            }'),
    'answer-inside-call-retry-without-gate': ('            s->retry_ms = s->requested_ms + ZBLP_LINK_RETRY_MS;\n        } else if (!confirmed) {', '            s->retry_ms = s->requested_ms;\n        } else if (!confirmed) {'),
    'answer-not-waiting': ('            if (answered) {\n                s->waiting = true;\n', '            if (answered) {\n'),
    'answer-refunds-attempts': ('            if (answered) {\n                s->waiting = true;', '            if (answered) {\n                s->attempts = 0;\n                s->waiting = true;'),
    'answer-inside-call-reads-old-observation': ('s->observed_idle &&\n                              s->observation_sequence != s->request_observation;', 's->observed_idle;'),
    'failover-after-one-request': ('s->attempts < ZBLP_LINK_FAILOVER_ATTEMPTS', 's->attempts < ZBLP_LINK_FAILOVER_ATTEMPTS - 1U'),
    'failover-after-three-requests': ('s->attempts < ZBLP_LINK_FAILOVER_ATTEMPTS', 's->attempts < ZBLP_LINK_FAILOVER_ATTEMPTS + 1U'),
    'failover-ignores-settlement': ('    const bool settling = s->preference_valid &&', '    const bool settling = false && s->preference_valid &&'),
    'failover-settlement-one-tick-long': ('(uint32_t)(now - s->accepted_ms) < ZBLP_LINK_SETTLE_MS;\n    if (settling)', '(uint32_t)(now - s->accepted_ms) <= ZBLP_LINK_SETTLE_MS;\n    if (settling)'),
    'failover-ignores-a-matching-link': ('s->fail_over || !fast || !can || matched ||', 's->fail_over || !fast || !can || (matched && false) ||'),
    'failover-ignores-the-configured-range': ('s->fail_over || !fast || !can || matched ||', 's->fail_over || !fast || (!can && false) || matched ||'),
    'failover-counts-idle-requests': ('!can || matched || !s->mode_valid || !s->fast ||\n        s->attempts', '!can || matched ||\n        s->attempts'),
    'failover-for-an-idle-request': ('s->fail_over || !fast || !can || matched ||', 's->fail_over || (!fast && false) || !can || matched ||'),
    'failover-keeps-attempts': ('    s->fail_over = true;\n    s->attempts = 0;', '    s->fail_over = true;'),
    'failover-keeps-preference': ('    s->attempts = 0;\n    s->preference_valid = false;\n    s->waiting = false;\n    return true;', '    s->attempts = 0;\n    s->waiting = false;\n    return true;'),
    'failover-keeps-waiting': ('    s->preference_valid = false;\n    s->waiting = false;\n    return true;', '    s->preference_valid = false;\n    return true;'),
    'failover-survives-disconnect': ('memset(&p->slot[slot], 0, sizeof(p->slot[slot]));', '{ const bool f = p->slot[slot].fail_over; memset(&p->slot[slot], 0, sizeof(p->slot[slot])); p->slot[slot].fail_over = f; }'),
    'failover-reads-wrong-slot': ('return slot < ZBLP_LINK_SLOTS && p->slot[slot].fail_over;', 'return slot < ZBLP_LINK_SLOTS && p->slot[0].fail_over;'),
    'failover-folds-an-out-of-range-slot': ('    if (slot >= ZBLP_LINK_SLOTS) {\n        return false;\n    }\n    struct zblp_link_slot *s = &p->slot[slot];\n    if (s->fail_over ||', '    struct zblp_link_slot *s = &p->slot[slot % ZBLP_LINK_SLOTS];\n    if (s->fail_over ||'),
    'failed-over-folds-an-out-of-range-slot': ('return slot < ZBLP_LINK_SLOTS && p->slot[slot].fail_over;', 'return p->slot[slot % ZBLP_LINK_SLOTS].fail_over;'),
}
results = []
for name, change in [('baseline', None), *faults.items(), ('baseline-after', None)]:
    folder = out / name
    folder.mkdir()
    text = source
    if change:
        old, new = change
        assert text.count(old) == 1, (name, old)
        text = text.replace(old, new)
    path = folder / 'link_policy.c'
    path.write_text(text)
    exe = folder / 'test'
    cmd = ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic',
           '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
           '-I' + str(root / 'src'), str(path), str(root / 'tests/host/test_link_policy.c'), '-o', str(exe)]
    compiled = subprocess.run(cmd, capture_output=True, text=True)
    (folder / 'build.log').write_text(compiled.stdout + compiled.stderr)
    assert compiled.returncode == 0, (name, compiled.stderr)
    run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=20)
    (folder / 'run.log').write_text(run.stdout + run.stderr)
    assert (run.returncode != 0) == (change is not None), name
    if change:
        assert 'Assertion' in run.stdout + run.stderr, (name, 'not an assertion failure')
    results.append(dict(name=name, command=cmd, build_exit=compiled.returncode, run_exit=run.returncode))
    (out / 'results.json').write_text(json.dumps(results, indent=2))
print('PASS: idle link baseline before/after and %d compiled behavioral negative controls' % len(faults))
