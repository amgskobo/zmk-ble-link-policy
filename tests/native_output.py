#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Common native ELF admission: sanitizers, exit status and complete cases."""
import argparse
from pathlib import Path
import re
import subprocess
import sys


# UBSan's GCC form has a source location. A test label such as "runtime error:"
# without one is ordinary output. ASan/LSan and Clang use their named reports.
SANITIZER = re.compile(
    r"(?:ERROR|WARNING|SUMMARY):\s*(?:AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer)\b"
    r"|(?:AddressSanitizer|UndefinedBehaviorSanitizer):\s*DEADLYSIGNAL\b"
    r"|^[^\n]+:\d+(?::\d+)?:\s*runtime error:",
    re.MULTILINE,
)


def check_run(output, returncode, cases, *, expect_assertion=False):
    """Retain the baseline and named runtime-fault gates, rejecting diagnostics."""
    diagnostic = SANITIZER.search(output)
    if diagnostic:
        raise AssertionError("Sanitizer diagnostic in native output: " + diagnostic.group(0))
    if expect_assertion:
        assert returncode != 0 and "Assertion failed" in output, "Runtime assertion control did not fail"
    else:
        assert returncode == 0 and "PROJECT EXECUTION SUCCESSFUL" in output, "Native execution failed"
        summary = "pass = %d, fail = 0, skip = 0, total = %d" % (cases, cases)
        assert summary in output, "Missing complete native case summary: " + summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--cases", type=int, required=True)
    args = parser.parse_args()
    if args.cases <= 0:
        parser.error("--cases must be positive")
    try:
        run = subprocess.run([str(args.binary), "-no-rt"], stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, text=True, timeout=30)
    except subprocess.TimeoutExpired as error:
        if error.stdout:
            sys.stdout.buffer.write(error.stdout)
        raise
    print(run.stdout, end="", flush=True)
    check_run(run.stdout, run.returncode, args.cases)


if __name__ == "__main__":
    main()
