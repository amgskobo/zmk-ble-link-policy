# Owner tests

Read README.md, AGENTS.md and maintenance.json first. Resolve exact unchanged
stock/DYA ZMK, Zephyr and CMSIS revisions; no network update is implicit.
All executable assertions and fixtures below belong to this module.

## Reproducing CI

The GitHub workflow dispatches these same owner entries in a digest-pinned SDK
image. It runs four complete lanes: host, native on the common pinned Zephyr,
and firmware on unchanged stock ZMK and DYA. Native executes every fault and
both default/fast fixtures; it is not a reduced selection. There is no private
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
are resolved. This selection retains the XIAO BLE/Studio and native fixture
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
times. Any source drift makes the lane fail. Existing per-case logs, ordered
timing and binaries remain in the lane output. The hosted workflow uploads
those records and preparation failures even after a failed test, including
hidden `.config` files. Hosted execution remains a separate result from local
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
The native fixture retains the original 27 default cases and adds four
request-yield/reselection/info-race/clock cases (31 default total), plus 9 fast-interval cases, repeated three
times per binary. Kernel queues/timers are real; Bluetooth and ZMK endpoints
are recorded boundaries. The original 37 plus three request-quota/continuation
faults (40 compiled coordinator faults total) must build and fail
runtime assertions. A compilation error never counts as a killed fault.

Integration builds enabled without Studio/Messenger, diagnostics enabled, enabled with Studio and
disabled on each core, plus a normal peripheral and a peripheral that requests
the unsupported option. The latter must emit the specific dependency warning
and still build with the option, production objects and policy symbol absent.
All cases validate actual config, sources and symbols. The
integrating keyboard separately checks central-only source selection and the
settings-reset exemption with all its local overrides.

The peripheral-rejection check isolates the policy's own warning paragraph and
normalizes whitespace because Kconfig wraps it according to path length. It
still requires the assigned-y/actual-n result and the exact role expression;
another symbol's warning cannot supply them. Host negative controls preserve
these requirements alongside the effective config, source and symbol checks.

Two original traffic/refusal cases and the frame-work-storm fault stay in
Messenger's tests/native/link-policy-composition, where the actual Messenger
weak observer is composed with this production policy. This owner's tests do
not dispatch or import that sibling suite. tests/migration.json maps every
transferred fixture/fault family and the preserved composition assertions.

Physical negotiated parameters, transition latency, five-host HID, transfer,
radio duty, power and reset acceptance remain separate and unmeasured here.

The independent parameter-notification regression fails under the compiled
`unrelated-param-wake` control. That fault survived the initial fixture split;
its raw passing mutant and the added assertion are preserved in task evidence.
The shared timeout helper retains numeric/width/coverage/fault assertions here;
Messenger's TX-stage span assertions remain only with that distinct owner.

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
