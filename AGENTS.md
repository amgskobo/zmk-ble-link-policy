# ZMK BLE Link Policy: agent guide

Own the selected-host active/idle connection policy, its lifecycle coordinator,
numeric observations and direct tests. Support both unchanged stock ZMK and
DYA. Keep documentation and reviews focused on this module's connection policy,
lifecycle, diagnostics and tests. Do not add unrelated application dependencies,
migration instructions or coexistence requirements. Keyboard wiring, enabled
module graphs, production defaults and split/HID composition belong to the
integrating keyboard. Keep the project's documented translations consistent.

## Publication privacy and local execution

This rule applies to information prepared for publication or external sharing.
During an authorized task, agents may inspect and use actual local paths,
configuration, host/terminal/device identities and private test evidence as
needed for implementation, diagnosis, identity matching and validation. Such
local access does not itself authorize collection outside the task or disclosure.

Before preparing a public commit, push, PR, release, documentation/site output
or CI artifact, review the actual publication payload, including source,
comments, AGENTS files, examples, test fixtures, logs, screenshots, generated
files and metadata. Exclude user-related names and preferences, private absolute
paths and local-environment details, workstation/terminal identities, individual
device labels, ports, serials, addresses, pairing/bond identities, credentials,
and user- or device-specific test/measurement histories. Also check generated
and uploaded artifacts; checking source prose alone is insufficient.

Use repository-relative paths, documented variables/placeholders, neutral names
and clearly synthetic fixtures in public material. Declared public project
identifiers, existing attribution, standard API/product/platform names and
reproducible support requirements remain technical project information; do not
present one user's actual environment as a project requirement. In particular,
documenting supported platforms is distinct from identifying a user's device.

Keep exact originals and identifiers in appropriate private/local task records
when required for accurate evidence or device matching. Produce a separate
sanitized publication copy without weakening assertions or inventing results;
do not describe it as the unchanged raw record. If useful evidence cannot be
shared without exposing these details, keep it private and state the public
validation limit. Recheck the final publication snapshot and its artifacts.
Existing authorization requirements for version-control, publication, device
writes, resets and data erasure still apply.

## Before editing

Keep changes local and preserve unrelated work. Staging, commits, pushes,
PR creation/updates, merges, tags, publication, flashing and settings/bond
erasure require explicit user authorization. Dependency instructions grant
none. Authorization already given for the same action and scope remains valid;
do not request it again merely because a new step or turn begins.

Read [README.md](README.md), [tests/README.md](tests/README.md) and
[tests/maintenance.json](tests/maintenance.json). Use
[tests/ci/pins.json](tests/ci/pins.json) for the exact supported test graph.
Read [tests/migration.json](tests/migration.json) before moving or retiring
fixtures. Use the relevant run's evidence when citing measurements.
Keep English/Japanese installation settings, units, dependencies,
resource costs, runtime limits and remeasurement instructions consistent.

Resolve actual west imports, explicit module selections/local overrides,
enabled Kconfig, backend and dependency revisions. A sibling checkout or cache
name does not identify what a container builds. Read relevant root/nested
dependency instructions and required documents once per checkout/revision,
including selected ZMK/Zephyr, HAL, crypto and test infrastructure.
Record paths/revisions and READ, NO AGENTS or INACCESSIBLE. Block only dependent
checks and continue independent work. Never patch selected dependencies, disable
required features or implicitly update pins/caches to clear a gate.

## Policy contract

- The selected HID endpoint determines active mode. USB/no endpoint means no
  active BLE host. Manage logical profiles 0..4 only: known connected hosts in
  the peripheral role on the default identity. Exclude guests, other identities,
  disconnected objects and split/central links.
- Application traffic does not promote a host.
  Advertising, profiles/bonds, PHY, packet sizes, transfer protocols, HID report
  arbitration stay with their owners. Integrators own the configured
  `BT_PERIPHERAL_PREF_*` values and connection-object capacity.
- Preserve selected-first reconciliation, at most one parameter API call per
  worker, and immediate continuation of ready deferred requests after reading
  the current endpoint again. Deferral consumes no attempt and does not widen
  an idle request. A call-count bound is not a bound on a synchronous API call.
- Preserve per-host independence, request/observation generations, stale-scan
  fences, disconnect/reset handling and latest-selection coalescing. Retain the
  three-attempt budget, five-second command gate, twenty-second accepted-request
  settlement and six-second idle grace unless the task explicitly changes them.
  Update the contract and affected fixtures with an intentional behavior change.
- Keep fixed bounded storage, no heap/new thread or idle periodic work, and no
  connection reference retained between scans. Preserve lock/API boundaries and
  test queue, cancellation and recovery behavior when scheduling changes.
- Diagnostics remain default off and numeric only; never log addresses, keys,
  input values or payloads. Keep invalid/unavailable observations distinct from
  valid zero, and retain schema/width, sum/max/call-count and sequence guards.

## Required checks

Run commands from this checkout using the supported test environment in
[the reproduction guide](tests/README.md#reproducing-ci). Derive environment
requirements from that public procedure rather than a contributor's workstation.

| Change | Required validation |
| --- | --- |
| Documentation/instructions only | `git diff --check`, changed links/command references and EN/JA consistency. Without Git history, check the before/after diff with `git diff --no-index --check`. |
| Pure policy, numeric parser or timeout helper | Full host entry and affected named controls; include native/both-core checks when the coordinator interface or timing boundary changes. |
| Lifecycle, queues, Kconfig, build/module integration or public interfaces | Full host, native/mutation and unchanged stock/DYA firmware entries below; retain every existing integration mode, diagnostic setting, role and rejected-request case. |
| CI runners, dependency selection or test infrastructure | Complete four-lane owner matrix, affected positive/negative guards and documented preparation paths. No `--only` result qualifies as the full matrix. |

```sh
python3 tests/run-ci.py --suite host --output NEW_HOST_OUTPUT
python3 tests/run-ci.py --suite native --workspace RESOLVED_STOCK --output NEW_NATIVE_OUTPUT
python3 tests/run-ci.py --suite integration --core upstream --workspace RESOLVED_STOCK --output NEW_STOCK_OUTPUT
python3 tests/run-ci.py --suite integration --core dya --workspace RESOLVED_DYA --output NEW_DYA_OUTPUT
```

The native fixture uses the shared pinned Zephyr; real stock/DYA firmware
builds separately validate their public APIs and role/configuration gates.
Both supported cores are required. Network preparation is an explicit
`tests/prepare-workspace.py --allow-network` operation into a new workspace;
`--from-cache` clones exact local objects without fetching/updating the source
cache. Existing output/workspace directories stop the run. Do not change pins
or reuse an old result as a shortcut. The integrating keyboard owns its normal
role/reset builds and production feature/source validation.

## Tests and evidence

Keep executable assertions, independent oracles, fixtures, models, mutations,
selection/configuration/build validators and their metadata in this owner's
`tests/`. CI/package scripts dispatch these entries. Sibling suites, private
backups and source-rewriting adapters do not supply missing assertions. Guarded
production probes expose state, not scenarios. Map implementation families to
named normal/boundary/invalid, independent-host, ordered-time/output,
reset/cancellation, recovery and role/protocol cases and remaining gaps.

Regressions must fail before repair or under a named control. A failed compile
is not a killed runtime fault. Preserve unique assertions and migration/retirement
maps before removing fixtures. Quick retains all behavioral scenarios; full
retains relevant sanitizer, optimization, ABI, coverage, kernel and core matrices.
Never weaken assertions/features or accept changed snapshots merely to pass.

Use fresh outputs and retain commands, source/dependency/feature/backend/image
identities, actual exits, raw times and ordered per-report output. Totals do not
replace individual order/timing. Keep matching binaries, configs and DTS.
Do not edit a running script; source drift invalidates that qualification.
Use fixed-input traces or specification-based timing bounds. Repeat an identical
binary/input/seed/PHY/initial-flash run at least three times before calling a
mismatch nondeterministic; host load alone is not an explanation. Keep necessary
diffs, identities and failures without rolling source backups.

Report PASS, FAIL, BLOCKED, NOT RUN and unsupported NOT APPLICABLE separately.
Local CI-equivalent success is not a hosted Actions result or authorization to
publish. Host/native/build checks do not establish physical negotiation, HID,
radio, split, power or reset acceptance. Completed-worker wall time includes
preemption/blocking, excludes final summary-log cost and is not CPU/RF/HID time.
Separate boots and preserve invalid, stale, incomplete and unavailable data.
Pair device records with actual host HID arrivals before making end-to-end
claims. A successful read or quiet log is not a stability verdict and authorizes
no restart, erasure or traffic-policy change.

## Container test execution

Run automated tests, builds and dependency setup inside Docker. Host entry points
may launch containers and export results; do not run test runtimes or install test
packages/toolchains on the host. Owner source may be mounted read-only. Keep
test-only ZMK/Zephyr/tool checkouts, runtime environments, temporary files, build
outputs and caches in container images or named volumes. Tests must not require
host sibling dependency checkouts or private absolute paths.

Resolve and verify the selected revisions inside the container. Prepare missing
dependencies explicitly in an isolated container workspace; do not silently update
shared caches or pins. Preserve all owner scenarios, assertions and required
matrices. Hardware acceptance needs documented device access; report unavailable
container transports as BLOCKED rather than substituting host execution or mocked
acceptance. Export only reviewed evidence and retain the publication privacy rule.

## Fast container test execution

Use prepared container environments, persistent caches and bounded parallelism
to reduce execution time. Preserve every required scenario, assertion, negative
control, sanitizer, supported-core variant and repeat.

- Separate environment preparation from test execution. Pin container images by
  digest and resolve dependency revisions explicitly. Reuse prepared environments
  only when their image, toolchain, dependency revisions and lockfiles match.
  Do not fetch or update dependency inputs during an ordinary test run. Missing
  inputs block only dependent checks.

- Keep dependency checkouts, installed test environments, temporary files, build
  directories and caches in container images or named volumes. Prefer Linux
  filesystem storage for intensive file access. Any staged owner sources must
  preserve all required test/compiler inputs, including relevant untracked files,
  with source identity and pre/post-run drift verification.

- Reuse explicitly selected compiler and package caches with their supported
  identity and concurrency checks. Keep incompatible environments separate.
  Never share writable build directories or mutable installed dependency trees
  between independent jobs. Preserve required clean-cache checks; use isolated
  fresh caches for those checks.

- Dispatch each owner's canonical test entries from inside the container.
  Common orchestration may select, schedule and collect those entries; assertions
  and fixtures remain in the owning repository. Avoid nested Docker launches and
  repeated environment preparation for compatible jobs. Isolate each job's
  environment, working files and outputs.

- Enforce one configurable CPU and memory budget across concurrent suites,
  targets, compiler jobs and test workers. Account for nested parallelism.
  Parallelize only independent jobs; preserve required execution and output order.

- Within a run, build each required binary once per distinct input/configuration
  variant and reuse that exact binary for required identical-condition repeats,
  with each repeat's required initial state. Execute every required test; cached
  artifacts or previous PASS records do not establish a new test execution.

- Keep fresh build and evidence directories. Record actual commands, exits,
  completion markers, ordered output, source/dependency/image identities and
  cache configuration. Measure preparation, staging, build, execution and
  validation separately.

Quick and full retain their existing contracts. Interface/infrastructure changes
still require the complete owner matrix. Evaluate speed changes using equivalent
inputs and checks, report cold and warm runs separately, and retain failures.
