# Owner tests

Read README.md, AGENTS.md and maintenance.json first. Resolve exact unchanged
stock/DYA ZMK, Zephyr and CMSIS revisions; no network update is implicit.
All executable assertions and fixtures below belong to this module.

## Reproducing CI

The GitHub workflow dispatches these same owner entries in a digest-pinned SDK
image. It runs four complete lanes: host, native on the common pinned Zephyr,
and firmware on unchanged stock ZMK and DYA. Native executes every fault and
all default/fast/preemptible fixtures; it is not a reduced selection. There is no private
cache, sibling checkout, automatic scheduled fetch or integration-owner probe.
The image supplies Python/PyYAML, west, CMake, GNU C, multilib/gcov and the
Zephyr SDK. Direct commands on another host require equivalent tools; they
do not install packages automatically.

Use the image in [ci/pins.json](ci/pins.json). From a shell at this checkout,
choose a new named volume for dependencies, runtime files and results. Source
is read-only. Network access belongs only to the explicit preparation phase:

```sh
ZBLP_RESULTS_VOLUME=zblp-validation-new
docker volume create "$ZBLP_RESULTS_VOLUME"
docker run --rm --cpus 2 --memory 4g \
  --mount "type=bind,source=$PWD,target=/src,readonly" \
  --mount "type=volume,source=$ZBLP_RESULTS_VOLUME,target=/work" --workdir /src \
  zmkfirmware/zmk-build-arm@sha256:edb1c953438c6f720ddb79c3762f3972013b7fbbaf4fff3592fc869983e7afc5 \
  bash -euc 'python3 tests/prepare-workspace.py --core upstream --output /work/upstream --allow-network
             python3 tests/prepare-workspace.py --core dya --output /work/dya --allow-network'
docker run --rm -it --network none --cpus 2 --memory 4g \
  --mount "type=bind,source=$PWD,target=/src,readonly" \
  --mount "type=volume,source=$ZBLP_RESULTS_VOLUME,target=/work" --workdir /src \
  zmkfirmware/zmk-build-arm@sha256:edb1c953438c6f720ddb79c3762f3972013b7fbbaf4fff3592fc869983e7afc5 bash
```

Inside that container:

```sh
python3 tests/run-ci.py --suite host --output /work/results/host
python3 tests/run-ci.py --suite native --workspace /work/upstream --output /work/results/native
python3 tests/run-ci.py --suite integration --core upstream --workspace /work/upstream --output /work/results/upstream
python3 tests/run-ci.py --suite integration --core dya --workspace /work/dya --output /work/results/dya
```

Network preparation requires `--allow-network` and a new directory. It fetches
only the ten exact project commits in the owner manifest; no moving imports
are resolved. This selection retains all required firmware and native fixture
features. It is a test workspace, not a replacement for a keyboard's manifest.
With an existing exact workspace use `--from-cache ABS_WORKSPACE` instead;
new local clones materialize only each selected commit without fetching or
updating the source cache. They retain no borrowed object paths and remain
readable after that cache is unmounted. Unavailable unrelated history is not a
test input. Reused outputs fail before work starts.

Keep the volume until its evidence has been reviewed. Export only the selected
reviewed results; container logs, build directories and physical captures do not
automatically become publication artifacts.

The CI entry records the module head (or no Git history), all source hashes
before/after, dependency identities, actual command exit and raw start/end
times. Dependencies must match their pinned commits with clean tracked files
and no untracked source; ignored runtime caches remain allowed. The lane checks
dependency revisions and cleanliness again after execution. Owner or dependency
source drift makes the lane fail. Existing per-case logs, ordered
timing and binaries remain in the lane output. Outside the review branch, the
hosted workflow uploads those records and preparation failures even after a
failed test, including hidden `.config` files. On `codex/ble-link-policy-review`,
raw evidence uploads are disabled; review local evidence before sharing selected
records. Hosted execution remains a separate result from local
execution of the same commands. No command here flashes or modifies settings.

## Direct owner entries

```sh
python3 tests/run-host.py --output NEW_ABSOLUTE_OUTPUT
ZEPHYR_BASE=RESOLVED/zephyr ZEPHYR_TOOLCHAIN_VARIANT=host \
  ZBLP_NATIVE_CMAKE_ARG=-DZEPHYR_MODULES=RESOLVED/modules/hal/cmsis \
  ZBLP_NATIVE_BUILD_DIR=NEW_ABSOLUTE_OUTPUT bash tests/native/run.sh
python3 tests/mutation/mutate_link_service.py --zephyr-base RESOLVED/zephyr \
  --cmsis-module RESOLVED/modules/hal/cmsis --output NEW_ABSOLUTE_OUTPUT
python3 tests/run-integration.py --workspace RESOLVED_WORKSPACE \
  --output NEW_ABSOLUTE_OUTPUT
```

Run the integration entry once for unchanged stock ZMK and once for unchanged
DYA, with explicit selected module roots from west list. The pinned cores are
stock `5b51501fead672c41b5cfb396f3dafe0894bf4e9` and DYA
`e5c9b6915b56801193e359dd9bad4a167ce0d1b8`, with Zephyr
`10ba6d0cb38bc3d258775d27982f707599320085`. Existing outputs fail.
Both native mutation and integration entries accept `--build-root` for a fresh
Linux filesystem build directory while keeping records/raw logs in `--output`.
Use `ZMK_TEST_MODE=full` with the host entry for timeout optimization, coverage
and 32-bit ABI variants as well as the normal sanitizer checks.

The inherited pure suite uses an independent literal selected-slot/time oracle,
invalid slots, independent host states, wrap, reset, request/observation gates,
refusal, idle grace, fallback and quiescence. Host runs include sanitizers and
optimization, three identical runs and all 59 compiled negative controls.
The native fixture retains the original 27 default cases and adds five
request-yield/reselection/info-race/clock/selection-boundary cases (32 default total), plus 9 fast-interval cases, repeated three
times per binary. Kernel queues/timers are real; Bluetooth and ZMK endpoints
are recorded boundaries. The original 37 plus four request-quota/continuation/selection
faults are retained. A third binary preserves all32 default cases under a
preemptible system workqueue and adds a real priority5 endpoint producer after
the quiet worker releases its lock (33 cases). It must admit that wake without
an external replay; the original self-cancel implementation is the added
`quiet-self-cancel-loses-wake` control (42 compiled coordinator faults total). All controls must build and fail
runtime assertions. A compilation error never counts as a killed fault.

Both native launchers use `native_output.py` to reject actual ASan/LSan reports,
Clang UBSan reports and GCC's source-located `runtime error:` diagnostics before
checking exits, success markers and complete case counts. A recovering sanitizer
can otherwise print a diagnostic, complete every case and exit zero. A diagnostic
also cannot qualify a coordinator fault that must fail its runtime assertion.
Locationless `runtime error:` test labels remain ordinary output.
The host lane runs `test_native_output.py` to preserve these admission rules.

The two named sanitizer-admission controls are separate from the42 coordinator
faults. They retain the real policy and all native cases, adding only an explicit
startup fault through the existing `ZBLP_LINK_SOURCE` test input. For example,
inside the prepared container, build the ASan control in a fresh directory:

```sh
ZEPHYR_BASE=RESOLVED/zephyr ZEPHYR_TOOLCHAIN_VARIANT=host \
  west build -p=always -s tests/native/idle-links -d NEW_ASAN_BUILD \
  -b native_sim/native/64 -- -DWEST=OFF -DZEPHYR_BASE=RESOLVED/zephyr \
  -DZEPHYR_MODULES=RESOLVED/modules/hal/cmsis \
  -DZBLP_LINK_SOURCE="$PWD/tests/native/sanitizer_control.c" \
  -DCONFIG_ASAN=y -DCONFIG_ASAN_RECOVER=y \
  -DEXTRA_CFLAGS=-DZBLP_ASAN_ADMISSION_CONTROL=1
python3 tests/native/check_admission.py \
  --binary NEW_ASAN_BUILD/zephyr/zephyr.exe --cases 32 --kind asan \
  --output NEW_ASAN_CONTROL_OUTPUT
```

For the UBSan control use another fresh build/output, replace the three ASan
settings with `-DCONFIG_UBSAN=y` and
`-DEXTRA_CFLAGS=-DZBLP_UBSAN_ADMISSION_CONTROL=1`, then select `--kind ubsan`.
The checker sets the recovering runtime options explicitly. Each control must
emit its real diagnostic, exit zero and complete all32 cases; the original
exit/marker/count predicate must accept it and fixed admission must reject it
specifically for that diagnostic in three fresh runs of the same ELF.
These are expected rejections, not clean product results. Retain actual compiler
arguments, effective config, dependency headers/objects and ELF identity before
running. This recovering-control build does not establish strict sanitizer or
optimization coverage; those require separately selected flags, including
`CONFIG_ASAN_RECOVER=n` and a final `-fno-sanitize-recover=all` for a strict build.
Zephyr and the separately compiled NSI runner need their own actual-argument
checks; NSI defaults to `-O0` even when Zephyr uses another optimization level.

Integration builds six configurations on each core: the policy enabled,
diagnostics enabled, the policy enabled with additional core features, the policy
disabled, a normal peripheral and a peripheral that requests the unsupported
option. The latter must emit the specific dependency warning and still build
with the option, production objects and policy symbol absent.
All cases validate actual config, sources and symbols. The
integrating keyboard separately checks central-only source selection and the
settings-reset exemption with all its local overrides.

The peripheral-rejection check isolates the policy's own warning paragraph and
normalizes whitespace because Kconfig wraps it according to path length. It
still requires the assigned-y/actual-n result and the exact role expression;
another symbol's warning cannot supply them. Host negative controls preserve
these requirements alongside the effective config, source and symbol checks.

Fixture provenance and assertion ownership are recorded in
[migration.json](migration.json). This module's test entries dispatch the
policy's own fixtures and assertions.

Physical negotiated parameters, transition latency, five-host HID, transfer,
radio duty, power and reset acceptance remain separate and unmeasured here.

The independent parameter-notification regression fails under the compiled
`unrelated-param-wake` control. That fault survived the initial fixture split;
its raw passing mutant and the added assertion are preserved in task evidence.
The timeout helper retains numeric/width/coverage/fault assertions here.

The selection boundary maps unmanaged BLE profiles and non-BLE routes to slot
255. The `unmanaged-selection-in-diagnostics` compiled control must fail the
native assertion. Real synthetic Git fixtures in `test_dependency_identity.py`
cover clean dependencies, staged/unstaged changes, deletions, untracked source,
ignored runtime caches and source/revision drift during an otherwise successful
lane. A dirty dependency must stop the command before it starts.

The pure capacity case checks that deferral preserves the attempt budget,
command gate, idle widening and grace. Real queued unrelated work must run
between each of five native requests, and a selection changed by that queued
work must take priority on continuation. Compile the instrumented unbounded
control and repeat its identical binary/input three times to preserve pre-fix
failures; the new assertions must fail while the original scenarios still pass.

For physical observations, record a fresh, serial-matched console using the
integrating keyboard's recorder, preserving image/boot identity and raw order:

```sh
python3 tests/observations.py --console CONSOLE.jsonl --output NEW_OUTPUT
```

The analyzer retains ordered raw rows, device timestamps, host UTC/capture
time, sequence wrap and peak provenance. It separates selected slot, host mask
and stale scans; invalid timing and missing observations never become zero.
The request limit is one by default. Only an explicitly identified instrumented
unbounded baseline may use `--request-limit 5`. Parser fixtures cover malformed,
duplicate, reordered, reset, extra/overflow and inconsistent timing fields.
Valid API timing requires maximum <= sum <= maximum * calls and sum <= work;
a single call therefore has equal sum and maximum.
Summaries include mean, nearest-rank p50/p95 and maximum for completed request
workers. They exclude no-call workers and final summary-log cost, and do not
measure CPU, RF, HID arrival, idle power or stability. Physical negotiation,
selection changes and paired HID capture require a matching installed image.

The `stale-scan-requests` fault survived after introducing the quota: earlier
fixtures changed selection only after the first request, so the quota masked
the removed generation guard. A new fixture changes selection during the
first information query and requires the first request to target the new
host. Preserve the surviving fault output as the named before-control.
Timing resolution follows the selected cycle clock; for example, a32768 Hz
clock has about30.52 us resolution. Zero can mean below that resolution. Use bounded
captures: spans of half a 32-bit cycle period or more are unsupported, and
multiple full wraps cannot be inferred from a 32-bit counter alone.
