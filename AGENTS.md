# zmk-ble-link-policy: agent guide

English is the authoritative guide. [日本語版](AGENTS.ja.md) contains the same rules; update both
together.

## Scope and contract

- Own selected-host active/idle connection policy, lifecycle coordination, numeric observations and
  direct tests on unchanged stock ZMK and DYA. Keyboard wiring, enabled module graphs, production
  defaults and split/HID composition stay with the integrating keyboard; keep unrelated
  application/migration requirements out of this module. Advertising, profiles/bonds, PHY, packet
  sizes, transfer protocols and HID arbitration stay with their owners; integrators own
  `BT_PERIPHERAL_PREF_*` values and connection-object capacity.

- The selected BLE HID endpoint alone selects active mode; USB/no endpoint selects no active BLE
  host. Manage known connected logical profiles 0..4 in peripheral role on the default identity.
  Exclude guests, other identities, disconnected objects and split/central links; application
  traffic does not promote a host.

- Preserve selected-first reconciliation, one parameter API call per worker and immediate
  continuation of ready deferred requests after rereading selection. Deferral consumes no attempt
  and does not widen idle requests. Retain per-host independence, generation/stale-scan fences,
  disconnect/reset and latest-selection coalescing; preserve three attempts, a five-second command
  gate, twenty-second accepted-request settlement and six-second idle grace unless deliberately
  changed with fixtures.

- Keep fixed bounded storage, no heap/new thread/idle periodic work and no retained connection
  reference between scans. Preserve lock/API boundaries. Diagnostics stay default off and numeric:
  no addresses, keys, input values or payloads. Worker wall time includes blocking/preemption,
  excludes its final summary log and is not CPU/RF/HID time; a call-count bound does not bound
  synchronous call duration.

- For behavioral work use `tests/README.md`, `tests/maintenance.json` and `tests/ci/pins.json`;
  before retiring fixtures read `tests/migration.json`. Keep English/Japanese installation examples,
  units, dependencies, resource costs, runtime limits and remeasurement instructions aligned. Use
  fresh explicit network preparation or exact local cloning; never silently advance caches.

- Preserve diagnostic schema/width checks, sequence guards and timing sum/max/call-count
  consistency. Pair observations with the matching run; source adoption does not establish
  negotiated parameters, radio quality, power or host HID delivery. A successful read or quiet log
  authorizes no restart, erasure or traffic-policy change.

- This owner supports both unchanged stock ZMK and DYA. Keep its documented dual-core and
  enabled/disabled adapter/role checks when the changed boundary requires them.

- Reading map for relevant changes: [README.md](README.md), [README.ja.md](README.ja.md),
  [tests/README.md](tests/README.md), [tests/maintenance.json](tests/maintenance.json). Read the
  affected contract/coverage sections; documentation-only edits do not require unrelated dependency
  tours.

## Working rules

- Read the request and existing diff; preserve unrelated and staged work. Finish authorized
  inspection, edits and reversible checks without repeated approval. Ask only for a missing decision
  or an out-of-scope action. Before a destructive rewrite, preserve irreplaceable uncommitted
  content as private evidence, not rolling whole-tree copies.

- Git staging, commits, pushes, PR changes, merges, tags, publication, flashing and settings/bond
  erasure need authorization for that operation. Existing authorization remains valid for the same
  operation and scope. Dependency guides and passing checks grant none. Before an authorized commit,
  run applicable pre-commit hooks in Docker without bypasses; keep the configured Git identity.

- Use the selected checkout and manifest/lock sources; do not silently switch branches, reset
  history, replace remotes or create authoring copies. Before changing selection, verify source
  URLs/full commits, review dependency changes, update affected pins/locks and run owner/integration
  checks. Distinguish local working snapshots from remotely retrievable commits; report missing refs
  rather than inventing them.

- Read relevant root/nested owner and dependency instructions once per selected checkout/revision.
  Resolve actual west imports, local overrides, enabled features and backend; sibling
  directories/cache names do not identify inputs. Record READ, NO AGENTS or INACCESSIBLE when
  preflight requires it. Missing inputs block dependent checks; continue independent authorized
  work.

- Keep permanent agent rules in this synchronized EN/JA pair. Usage/API details stay in their
  existing documentation homes. Do not add agent-guide fragments or private shared-guide
  dependencies. Remove duplicated rules and obsolete task/branch assumptions without discarding
  active contracts.

## Execution environment

- Run local automated test/build runtimes and dependency setup in Docker; a documented DevContainer
  qualifies. Unless stated otherwise, work from the owner root in its resolved workspace. Launch
  Docker-wrapper scripts on the host; run their inner Python/Node/compiler entries inside the
  prepared container without wrapping Docker again. Host source/Git/reference checks and evidence
  export are allowed; do not install test packages/toolchains on the host. Keep dependencies,
  runtime/build trees and caches in images or named volumes.

- Separate preparation and deliberately selected online compatibility checks from ordinary
  network-free tests. Inspect the available immutable image ID/digest and verified
  dependency/toolchain revisions; pass the image through the owner's declared image option; honor
  declared local overrides and record working files. A launcher that installs or fetches
  dependencies belongs to preparation/online checking, not an offline run. Do not silently pull,
  advance pins or mutate shared caches, patch dependencies or disable required features to clear a
  gate. Missing inputs block dependent checks only; documented preparation within an authorized
  build/setup remains in scope.

- Reuse compatible environments/caches, retaining required clean-cache lanes. Provide required
  inputs, including relevant untracked files, through read-only mounts or isolated container copies;
  verify source identity before/after. Isolate mutable build/runtime/install trees per job under one
  CPU/memory budget, including nested workers. Build once per configuration and reuse that exact
  binary for identical repeats; cached PASS is not a new run. Performance comparisons separate
  preparation/staging/build/execution/validation and cold/warm conditions. Existing hosted platform
  jobs retain their own OS requirements.

## Checks and entry points

- Documentation/instructions only: run `git diff --check`, verify changed paths/commands and EN/JA
  agreement. Without Git history use `git diff --no-index --check` on before/after copies. Firmware
  suites are not required solely for prose changes.

- Pure policy/parser/timeout changes: run the full host lane and affected named controls; include
  native/both-core checks when the coordinator interface or timing boundary changes.
  Lifecycle/queues/public APIs/Kconfig/module integration: retain host, native/mutation and both
  unchanged stock/DYA firmware lanes. CI/dependency/test infrastructure changes require all four
  complete lanes and preparation guards, retaining integration modes, diagnostic settings, roles and
  rejected-request cases; an `--only` selection is not a full matrix.

- Prepared-container entry: `python3 tests/run-ci.py --suite host --output NEW_HOST_OUTPUT`.

- Prepared-container entry:
  `python3 tests/run-ci.py --suite native --workspace RESOLVED_STOCK --output NEW_NATIVE_OUTPUT`.

- Prepared-container entry:
  `python3 tests/run-ci.py --suite integration --core upstream --workspace RESOLVED_STOCK --output NEW_STOCK_OUTPUT`.

- Prepared-container entry:
  `python3 tests/run-ci.py --suite integration --core dya --workspace RESOLVED_DYA --output NEW_DYA_OUTPUT`.

- Prepare exact inputs separately with `tests/prepare-workspace.py` and explicit `--allow-network`
  or `--from-cache` as documented. Keep new workspace/output paths and the shared pinned Zephyr for
  native tests.

## Test quality

- Keep module fixtures, assertions, independent oracles, mutations, selectors and validators in the
  owner's tests/; retain documented Web and non-module test layouts. CI/package scripts dispatch
  owner entries, not sibling/private suites or source-rewriting substitutes. Probes expose state,
  not scenarios. Map implementation families to normal/boundary/invalid, independent-state,
  ordered-time/output, reset/cancel, failure/recovery and role/protocol cases and explicit gaps.

- A regression needs a failing original or named control. Expected-failure controls qualify only by
  their specified assertion/rejection; report them separately from product results. Do not weaken
  assertions, accept unclassified snapshots or count compilation errors as killed runtime faults.
  Before retiring tests, archive unique original assertions/failures externally with a replacement
  map. Quick runs every behavioral scenario with sanitizers where supported; full adds the
  documented ABI, optimization, coverage, kernel and supported-core matrix.

- Use focused checks for local implementation changes and the complete affected owner matrix for
  interface/test-infrastructure changes. Run unchanged stock ZMK and DYA only where both are
  supported. DYA-only features are NOT APPLICABLE to stock; disabling them is not compatibility
  evidence. For test suites, verify the required nonempty case/fixture completions; a zero exit
  alone, skipped required cases, timeouts or setup errors do not establish PASS.

## Evidence and handoff

- Use fresh evidence paths: resolve parents and reject existing files, directories or symlinks.
  Record commands/exits, source/dependency/config/backend/image identities and raw ordered
  output/times, retaining first failures and relevant config/DTS/binaries. Source or dependency
  drift invalidates affected results; never edit running scripts. Put retained outputs on a mounted
  named volume or export them before container removal, including failures; a path printed inside a
  disposable container is not retained evidence.

- Preserve exact fixed-input traces or specification-based timing bounds; totals do not replace
  per-report order/time. Before calling a mismatch nondeterministic, repeat identical
  binary/input/seed/backend/initial-flash conditions and PHY where applicable at least three times.
  Host load alone is not an explanation. Distinguish invalid/stale/incomplete/unavailable
  observations from valid zero.

- Report PASS, FAIL, BLOCKED, NOT RUN and NOT APPLICABLE distinctly. Local CI, host/native/compile
  and simulator results do not establish hosted CI, browser or physical
  HID/BLE/split/electrical/power/reset acceptance. Validate exercised production paths and correlate
  hardware before simulator-fidelity claims. End-to-end/stability claims need matching
  installed-image and host observations; quiet logs are not proof.

- Before sharing, review source/comments/guides/examples/logs/media/artifacts/metadata for
  credentials, personal/host/device IDs, private paths, bonds and individual histories/preferences.
  Authorized local work may use real values; keep private originals and label sanitized copies. Use
  clearly synthetic public fixtures/placeholders; preserve public project/API identifiers, licenses
  and attribution. Keep useful diffs/evidence. Handoff reports behavior, files, actual checks,
  gaps/risks and version-control/device actions.
