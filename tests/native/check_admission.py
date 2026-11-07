#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Reject a built, recovering sanitizer control while preserving all native cases.

Build sanitizer_control.c through ZBLP_LINK_SOURCE first. This entry never
rebuilds its ELF and records three fresh process states with identical inputs.
The old exit/marker/count predicate must accept each output; the shared fixed
admission must reject specifically for its sanitizer diagnostic.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from native_output import check_run


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--cases", type=int, required=True)
    parser.add_argument("--kind", choices=("asan", "ubsan"), required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    binary_hash = hashlib.sha256(args.binary.read_bytes()).hexdigest()
    env = dict(os.environ, ASAN_OPTIONS="halt_on_error=0:detect_leaks=0",
               UBSAN_OPTIONS="halt_on_error=0:print_stacktrace=1")
    rows = []
    for repeat in range(3):
        command = [str(args.binary), "-no-rt"]
        started = time.time_ns()
        run = subprocess.run(command, env=env, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, text=True, timeout=30)
        finished = time.time_ns()
        (args.output / ("run-%d.log" % repeat)).write_text(run.stdout)
        # This is the original baseline admission predicate. The control must
        # pass all of it before a diagnostic rejection qualifies.
        assert run.returncode == 0 and "PROJECT EXECUTION SUCCESSFUL" in run.stdout
        assert ("pass = %d, fail = 0, skip = 0, total = %d" %
                (args.cases, args.cases)) in run.stdout
        signature = "ERROR: AddressSanitizer:" if args.kind == "asan" else "runtime error: signed integer overflow"
        assert signature in run.stdout, "Missing real recovering diagnostic"
        try:
            check_run(run.stdout, run.returncode, args.cases)
        except AssertionError as error:
            assert str(error).startswith("Sanitizer diagnostic in native output:"), str(error)
            rejection = str(error)
        else:
            raise AssertionError("Recovering diagnostic was admitted")
        assert hashlib.sha256(args.binary.read_bytes()).hexdigest() == binary_hash
        rows.append(dict(command=command, start_ns=started, end_ns=finished,
                         exit=run.returncode, original_predicate="ACCEPTED",
                         fixed_predicate="REJECTED", rejection=rejection,
                         binary_sha256=binary_hash,
                         output_sha256=hashlib.sha256(run.stdout.encode()).hexdigest()))
        (args.output / "results.json").write_text(json.dumps(rows, indent=2) + "\n")
    print("PASS: %s admission control rejected in three identical-ELF fresh processes" % args.kind)


if __name__ == "__main__":
    main()
