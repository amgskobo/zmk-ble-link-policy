#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Three fixtures on the real kernel: default (12..12), fast (7..7 with the idle
# 15 ms floor and failover), and default plus preemptible endpoint admission.
# Each binary is run three times.
set -euo pipefail
: "${ZEPHYR_BASE:?Set the resolved Zephyr checkout}"
: "${ZBLP_NATIVE_BUILD_DIR:?Set a new external build output directory}"
[ ! -e "$ZBLP_NATIVE_BUILD_DIR" ] || { echo 'Output already exists'; exit 2; }
[ ! -e "$ZBLP_NATIVE_BUILD_DIR-fast" ] || { echo 'Output already exists'; exit 2; }
[ ! -e "$ZBLP_NATIVE_BUILD_DIR-preempt" ] || { echo 'Output already exists'; exit 2; }
root="$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)"
build() {  # build directory, required cases, then extra CMake arguments
  local dir="$1" total="$2"; shift 2
  west build -p=always -s "$root/tests/native/idle-links" -d "$dir" \
    -b native_sim/native/64 -- -DWEST=OFF -DZEPHYR_BASE="$ZEPHYR_BASE" "${ZBLP_NATIVE_CMAKE_ARG:--DZEPHYR_MODULES=}" "$@"
  for run in 1 2 3; do
    python3 "$root/tests/native_output.py" "$dir/zephyr/zephyr.exe" --cases "$total"
  done
}
build "$ZBLP_NATIVE_BUILD_DIR" 32
build "$ZBLP_NATIVE_BUILD_DIR-fast" 9 -DZBLP_IDLE_LINKS_FAST=ON
build "$ZBLP_NATIVE_BUILD_DIR-preempt" 33 -DZBLP_IDLE_LINKS_PREEMPT=ON
