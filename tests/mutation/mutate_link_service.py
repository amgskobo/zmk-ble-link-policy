#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile native coordinator negative controls on the selected real kernel.

Three fixtures (tests/native/idle-links): default (12..12,32 tests), fast
(7..7 with idle floor/failover,9 tests), and preemptible (32 default plus quiet wake,33).
A fault is run against the fixture named in its
third element, 'default' when it has none.
"""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import time
import hashlib


def replace_fault_site(source, before, after, name):
    """Keep a directed C-token match independent of source indentation."""
    prefix, suffix = "", ""
    if name == "confirmed-param-wake-lost":
        # The adjacent wake/unlock pair is also used by other callbacks.
        # This fault belongs only to the parameter-observation callback.
        starts = list(re.finditer(r"^static\s+void\s+parameters\s*\(", source, re.M))
        ends = list(re.finditer(r"^static\s+void\s+security\s*\(", source, re.M))
        assert len(starts) == len(ends) == 1, (name, "callback boundaries")
        first, last = starts[0].start(), ends[0].start()
        assert first < last, (name, "callback order")
        prefix, source, suffix = source[:first], source[first:last], source[last:]
    tokens = re.findall(
        r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[A-Za-z_]\w*|'
        r"0[xX][0-9A-Fa-f]+[uUlL]*|[0-9]+[uUlL]*|>>=|<<=|->|\+\+|--|"
        r"==|!=|<=|>=|&&|\|\||<<|>>|\S",
        before,
    )
    pattern = r"\s*".join(re.escape(token) for token in tokens)
    matches = list(re.finditer(pattern, source))
    assert len(matches) == 1, (name, "source sites", len(matches))
    return prefix + re.sub(pattern, lambda _: after, source, count=1) + suffix


root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--zephyr-base", type=Path, required=True)
parser.add_argument("--cmsis-module", type=Path, required=True)
parser.add_argument(
    "--build-root",
    type=Path,
    help="Fresh Linux build root; records and raw logs remain in --output",
)
parser.add_argument(
    "--only",
    help="comma-separated fault names: run just these between the baselines (a partial run, not the full evidence)",
)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=False)
if args.build_root is not None:
    args.build_root.mkdir(parents=True, exist_ok=False)
source = (root / "src/link_service.c").read_text()
TESTS = {"default": 32, "fast": 9, "preempt": 33}
faults = {
    "quiet-self-cancel-loses-wake": (
        "if (delay != UINT32_MAX) {",
        "if (delay == UINT32_MAX) { (void)k_work_cancel_delayable(&link_work); } else {",
        "preempt",
    ),
    "unmanaged-selection-in-diagnostics": (
        "(unsigned)selected.ble.profile_index < ZBLP_LINK_SLOTS",
        "true",
    ),
    "unbounded-worker-batch": ("matched, now, !scan->issued);", "matched, now, true);"),
    "quota-reset-for-idle": (
        "scan.active_pass = false;",
        "scan.active_pass = false; scan.issued = false;",
    ),
    "deferred-continuation-lost": (
        "if (delay != UINT32_MAX)",
        "if (delay != UINT32_MAX && delay != 0U)",
    ),
    "lifecycle-all-addresses": (
        "if (slot < 0 || slot >= (int)ZBLP_LINK_SLOTS) {\n        return -1;",
        "if (false) {\n        return -1;",
    ),
    "lifecycle-ignores-role": (
        "info.role != BT_CONN_ROLE_PERIPHERAL || info.id != BT_ID_DEFAULT ||",
        "info.id != BT_ID_DEFAULT ||",
    ),
    "lifecycle-ignores-identity": (
        "info.role != BT_CONN_ROLE_PERIPHERAL || info.id != BT_ID_DEFAULT ||",
        "info.role != BT_CONN_ROLE_PERIPHERAL ||",
    ),
    "lifecycle-ignores-info-error": (
        "bt_conn_get_info(conn, &info) != 0 || info.type != BT_CONN_TYPE_LE ||\n        info.role != BT_CONN_ROLE_PERIPHERAL ||",
        "(bt_conn_get_info(conn, &info), false) || info.type != BT_CONN_TYPE_LE ||\n        info.role != BT_CONN_ROLE_PERIPHERAL ||",
    ),
    "lifecycle-wakes-failed-connect": (
        "if (err == 0 && lifecycle_slot(conn, true) >= 0)",
        "if (((void)err, lifecycle_slot(conn, true) >= 0))",
    ),
    "lifecycle-accepts-disconnected": (
        "(connected_only && info.state != BT_CONN_STATE_CONNECTED)",
        "(connected_only && false)",
    ),
    "lifecycle-connected-wake-lost": (
        "if (err == 0 && lifecycle_slot(conn, true) >= 0)",
        "if (false && err == 0 && lifecycle_slot(conn, true) >= 0)",
    ),
    "lifecycle-security-wake-lost": (
        "if (lifecycle_slot(conn, true) >= 0)",
        "if (false && lifecycle_slot(conn, true) >= 0)",
    ),
    "lifecycle-disconnect-requires-connected": (
        "const int slot = lifecycle_slot(conn, false);",
        "const int slot = lifecycle_slot(conn, true);",
    ),
    "lost-newer-selection": (
        "if (generation == wake_generation)",
        "if (generation <= wake_generation)",
    ),
    "idle-rejects-scaled-interval": (
        "#define IDLE_MAX_INTERVAL MAX(CONFIG_BT_PERIPHERAL_PREF_MAX_INT, 24U)",
        "#define IDLE_MAX_INTERVAL CONFIG_BT_PERIPHERAL_PREF_MAX_INT",
    ),
    "idle-ignores-lower-bound": ("info.le.interval >= min_interval &&", "true &&"),
    "refusal-recorded-as-accepted": (
        "if (err == 0) {\n        key = k_spin_lock(&policy_lock);",
        "if (err <= 0) {\n        key = k_spin_lock(&policy_lock);",
    ),
    "disconnected-request-accepted": (
        "fast, generation);",
        "fast, generation + (policy.slot[slot].generation - generation));",
    ),
    "confirmed-callback-lost": (
        "zblp_link_observed(&policy, (uint8_t)slot,",
        "zblp_link_observed(&policy, UINT8_MAX,",
    ),
    "idle-before-selected": (
        "if (selected != scan->active_pass) { return; }",
        "if (false && selected != scan->active_pass) { return; }",
    ),
    "stale-scan-requests": (
        "if (scan->generation != wake_generation)",
        "if (false && scan->generation != wake_generation)",
    ),
    "unrelated-param-wake": (
        "static void parameters(struct bt_conn *conn, uint16_t interval, uint16_t latency, uint16_t timeout)\n{",
        "static void parameters(struct bt_conn *conn, uint16_t interval, uint16_t latency, uint16_t timeout)\n{\n    wake();",
    ),
    "confirmed-param-wake-lost": (
        "            wake_locked();\n            k_spin_unlock(&policy_lock, key);",
        "            k_spin_unlock(&policy_lock, key);",
    ),
    "idle-never-wide": (
        "const bool wide = !fast && zblp_link_idle_wide(&policy, (uint8_t)slot);",
        "const bool wide = false && zblp_link_idle_wide(&policy, (uint8_t)slot);",
    ),
    "selected-may-be-wide": (
        ".interval_max = fast ? max_interval : (wide ? IDLE_MAX_INTERVAL : IDLE_FIRST_MAX_INTERVAL),",
        ".interval_max = (fast && !zblp_link_idle_wide(&policy, (uint8_t)slot)) ? max_interval : (wide || fast ? IDLE_MAX_INTERVAL : IDLE_FIRST_MAX_INTERVAL),",
    ),
    "idle-always-wide": (
        "const bool wide = !fast && zblp_link_idle_wide(&policy, (uint8_t)slot);",
        "const bool wide = !fast;",
    ),
    "selection-does-not-restart-timers": (
        "    if (current) {\n        /* A new selection restarts the fixed demotion delay of every previous host. */",
        "    if (false && current) {\n        /* A new selection restarts the fixed demotion delay of every previous host. */",
    ),
    "idle-answer-ignores-interval-bound": (
        "interval >= IDLE_MIN_INTERVAL && interval <= IDLE_MAX_INTERVAL && latency == IDLE_LATENCY",
        "interval >= IDLE_MIN_INTERVAL && latency == IDLE_LATENCY",
    ),
    "idle-answer-ignores-latency": (
        "interval >= IDLE_MIN_INTERVAL && interval <= IDLE_MAX_INTERVAL && latency == IDLE_LATENCY",
        "interval >= IDLE_MIN_INTERVAL && interval <= IDLE_MAX_INTERVAL && latency != CONFIG_BT_PERIPHERAL_PREF_LATENCY",
    ),
    "duplicate-pass-info-query": (
        "if (selected != scan->active_pass) { return; }",
        "if (selected != scan->active_pass) { struct bt_conn_info unused; (void)bt_conn_get_info(conn, &unused); return; }",
    ),
    # Failover and the 15 ms idle floor.
    "failover-for-a-production-range": (
        "FAST_CAN_FAIL_OVER, configured, now)",
        "true, configured, now)",
    ),
    "failover-never-decided": (
        "const bool failed_over = fast && zblp_link_fail_over(",
        "const bool failed_over = false && zblp_link_fail_over(",
        "fast",
    ),
    "failover-ignores-a-matching-link": (
        "FAST_CAN_FAIL_OVER, configured, now)",
        "FAST_CAN_FAIL_OVER, configured && false, now)",
        "fast",
    ),
    "failover-asks-the-configured-minimum": (
        "(failed_over ? ZBLP_LINK_COMPAT_MIN : CONFIG_BT_PERIPHERAL_PREF_MIN_INT)",
        "CONFIG_BT_PERIPHERAL_PREF_MIN_INT",
        "fast",
    ),
    "failover-asks-the-configured-maximum": (
        "(failed_over ? COMPAT_FAST_MAX_INTERVAL : CONFIG_BT_PERIPHERAL_PREF_MAX_INT)",
        "CONFIG_BT_PERIPHERAL_PREF_MAX_INT",
        "fast",
    ),
    "idle-minimum-follows-the-selected-minimum": (
        "CONFIG_BT_PERIPHERAL_PREF_MIN_INT)\n                                       : IDLE_MIN_INTERVAL;",
        "CONFIG_BT_PERIPHERAL_PREF_MIN_INT)\n                                       : CONFIG_BT_PERIPHERAL_PREF_MIN_INT;",
        "fast",
    ),
    "idle-first-request-uses-the-selected-maximum": (
        "(wide ? IDLE_MAX_INTERVAL : IDLE_FIRST_MAX_INTERVAL)",
        "(wide ? IDLE_MAX_INTERVAL : CONFIG_BT_PERIPHERAL_PREF_MAX_INT)",
        "fast",
    ),
    "failover-observation-keeps-the-configured-minimum": (
        "const uint16_t fast_min = failed_over ? ZBLP_LINK_COMPAT_MIN : CONFIG_BT_PERIPHERAL_PREF_MIN_INT;",
        "const uint16_t fast_min = CONFIG_BT_PERIPHERAL_PREF_MIN_INT;",
        "fast",
    ),
    "failover-observation-keeps-the-configured-maximum": (
        "const uint16_t fast_max = failed_over ? COMPAT_FAST_MAX_INTERVAL : CONFIG_BT_PERIPHERAL_PREF_MAX_INT;",
        "const uint16_t fast_max = CONFIG_BT_PERIPHERAL_PREF_MAX_INT;",
        "fast",
    ),
    "failover-state-read-from-another-slot": (
        "const bool failed_over = zblp_link_failed_over(&policy, (uint8_t)slot);\n            const uint16_t fast_min",
        "const bool failed_over = zblp_link_failed_over(&policy, 0);\n            const uint16_t fast_min",
        "fast",
    ),
    "idle-observation-floor-lost": (
        "interval >= IDLE_MIN_INTERVAL && interval <= IDLE_MAX_INTERVAL && latency == IDLE_LATENCY",
        "interval <= IDLE_MAX_INTERVAL && latency == IDLE_LATENCY",
        "fast",
    ),
}
env = dict(
    os.environ,
    ZEPHYR_BASE=str(args.zephyr_base),
    Zephyr_DIR=str(args.zephyr_base / "share/zephyr-package/cmake"),
    ZEPHYR_TOOLCHAIN_VARIANT="host",
    CMAKE_BUILD_PARALLEL_LEVEL=os.environ.get("CMAKE_BUILD_PARALLEL_LEVEL", "2"),
)
plan = [
    ("baseline", None, "default"),
    ("baseline-fast", None, "fast"),
    ("baseline-preempt", None, "preempt"),
]
only = set(args.only.split(",")) if args.only else None
assert only is None or only <= set(faults), sorted(only - set(faults))
plan += [
    (name, change[:2], change[2] if len(change) > 2 else "default")
    for name, change in faults.items()
    if only is None or name in only
]
plan += [
    ("baseline-after", None, "default"),
    ("baseline-fast-after", None, "fast"),
    ("baseline-preempt-after", None, "preempt"),
]
results = []
for name, change, variant in plan:
    out = args.output / name
    out.mkdir()
    path = out / "link_service.c"
    text = source
    if change:
        old, new = change
        text = replace_fault_site(text, old, new, name)
    path.write_text(text)
    build = (args.build_root / name) if args.build_root is not None else out / "build"
    cmd = [
        "west",
        "build",
        "-p=always",
        "-s",
        str(root / "tests/native/idle-links"),
        "-d",
        str(build),
        "-b",
        "native_sim/native/64",
        "--",
        "-DWEST=OFF",
        "-DZEPHYR_BASE=" + str(args.zephyr_base),
        "-DZEPHYR_MODULES=" + str(args.cmsis_module),
        "-DZBLP_LINK_SOURCE=" + str(path),
    ]
    if variant == "fast":
        cmd.append("-DZBLP_IDLE_LINKS_FAST=ON")
    elif variant == "preempt":
        cmd.append("-DZBLP_IDLE_LINKS_PREEMPT=ON")
    started = time.time()
    with (out / "build.log").open("wb") as log:
        compiled = subprocess.run(cmd, env=env, stdout=log, stderr=subprocess.STDOUT)
    assert compiled.returncode == 0, (name, "build failed; not a killed fault")
    runs = []
    for i in range(1 if change else 3):
        run = subprocess.run(
            [str(build / "zephyr/zephyr.exe"), "-no-rt"],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=30,
        )
        output = run.stdout
        (out / ("run-" + str(i) + ".log")).write_text(output)
        if change:
            assert run.returncode != 0 and "Assertion failed" in output, name
        else:
            assert run.returncode == 0 and "PROJECT EXECUTION SUCCESSFUL" in output, (
                name
            )
            total = TESTS[variant]
            assert (
                "pass = %d, fail = 0, skip = 0, total = %d" % (total, total) in output
            ), name
        runs.append(run.returncode)
    results.append(
        dict(
            name=name,
            variant=variant,
            command=cmd,
            build_exit=compiled.returncode,
            run_exits=runs,
            started=started,
            finished=time.time(),
            binary_sha256=hashlib.sha256(
                (build / "zephyr/zephyr.exe").read_bytes()
            ).hexdigest(),
        )
    )
    (args.output / "results.json").write_text(json.dumps(results, indent=2))
    print(name, variant, runs, flush=True)
print(
    "PASS: native baselines (default, fast and preemptible fixtures) repeated three times and %d compiled runtime fault controls%s"
    % (len(plan) - 6, " (PARTIAL: --only)" if only else "")
)
