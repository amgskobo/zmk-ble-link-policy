# ZMK BLE Link Policy

[日本語](README.ja.md)

Prioritize the selected BLE HID host while keeping other bonded host links
connected with peripheral latency 30. This module uses public ZMK
endpoint/profile and Zephyr connection APIs; no core edits, wrappers, heap,
persistent settings or idle periodic work.
Both unchanged stock ZMK and DYA are supported.
An independent optional policy adapts one outgoing split link to ZMK activity.
Host and split connections have separate role filters, state and retry budgets.

## Installation

Add the project to your existing `config/west.yml`, retaining your ZMK imports:

```yaml
manifest:
  remotes:
    - name: amgskobo
      url-base: https://github.com/amgskobo
  projects:
    - name: zmk-ble-link-policy
      remote: amgskobo
      revision: main
```

Run `west update zmk-ble-link-policy` and rebuild your keyboard. Pin a reviewed full
commit instead of `main` for reproducible production firmware.

For a local checkout, add its root with the shared build helper's
`-m /absolute/path/zmk-ble-link-policy`, or include it in the build's complete
`ZMK_EXTRA_MODULES`/`ZEPHYR_MODULES` selection. If the build explicitly selects
modules, adding a west project alone does not add it to that selection.

Enable it on the host-facing standalone keyboard or split central:

```conf
CONFIG_ZMK_BLE_LINK_POLICY=y
CONFIG_BT_PERIPHERAL_PREF_MIN_INT=12
CONFIG_BT_PERIPHERAL_PREF_MAX_INT=12
CONFIG_BT_PERIPHERAL_PREF_LATENCY=0
CONFIG_BT_PERIPHERAL_PREF_TIMEOUT=400
```

Interval units are 1.25 ms (12=15 ms,24=30 ms); latency counts skipped connection
events; timeout units are 10 ms (400=4 s). The example preserves the integrating
keyboard's existing preferences. The peripheral half is gated out. The option
defaults off and requires `ZMK_BLE` and `BT_PERIPHERAL`; it selects no dependency.
`CONFIG_ZMK_BLE_LINK_POLICY_LOG_LEVEL` is 0..4, default 3.

The managed logical profile range is 0..4. More configured profiles do not
extend the fixed table. This is a host-link policy, not a provider of BLE
profiles or additional connection objects. Keep enough `BT_MAX_CONN` capacity
for the keyboard's own hosts, split, advertising and reconnect requirements.

### Compatibility and tests

The integration matrix selects these unchanged commits:

| Core | Selected test revision |
| --- | --- |
| Stock ZMK | `5b51501fead672c41b5cfb396f3dafe0894bf4e9` |
| DYA | `9f75b536cff5a63d34cba43de9be8652b8bd1915` |
| Shared Zephyr | `10ba6d0cb38bc3d258775d27982f707599320085` |

[The complete test pins](tests/ci/pins.json) also fix the supporting libraries,
core build dependencies and SDK image digest.
Other core revisions need their own integration results. The module does not
depend on a sibling checkout, AKKB46 configuration or a private build cache.
Network preparation requires every selected commit to be retrievable by a fresh
runner. An exact clean local checkout can supply offline preparation; that does
not establish remote availability or hosted CI.
See [reproducing CI](tests/README.md#reproducing-ci),
[the test contract](tests/maintenance.json) and
[validation and hardware acceptance](docs/validation.md). A hosted CI badge is not
provided before an actual hosted run.

## Host behavior and ownership

The actual selected HID endpoint determines active mode. USB or no endpoint
means no active BLE HID host. Only connected known host profiles on the default
identity in the peripheral role are eligible; split-central links, guests,
other identities and disconnected objects are excluded. Application traffic
does not promote a host.

The selected host is reconciled first. Its preference comes from the four
`BT_PERIPHERAL_PREF_*` options above. Other hosts request latency 30 and retain
the configured supervision timeout. Idle intervals start at max(MIN_INT,12),
with max(MAX_INT,that minimum) on the first request. If unconfirmed, later idle
requests allow max(MAX_INT,24) until disconnect. Observed idle intervals within
the full compatible range are accepted. A selected interval below 12 that is
not applied after two settled requests falls back to 12..max(MAX_INT,12) until
disconnect. A configured minimum of 12 or higher does not use that fallback.

Promotion has no idle grace delay. A previously selected host's demotion waits
six seconds after the last endpoint selection change. Each mode gets at most
three requests, with a separate five-second command gate and twenty-second
accepted-request settlement bound. API success is a request, not evidence that
the peer applied it. Matching callbacks, disconnect generations and stale-scan
fences preserve current intent. A peer can refuse or revise the parameters;
the module cannot cancel a request already accepted by the stack.

No advertisements, profiles, bonds, PHY, packet sizes or transfer sessions are
owned here. The selected platform's own initial parameter update remains a
platform action; additional application policy providers must not independently
fight this one.

## Resources and acceptance

Each system-workqueue invocation issues at most one connection-parameter API
call. A ready request deferred by that limit consumes no attempt and schedules
an immediate continuation; the continuation reads the current endpoint again.
The selected host stays first. The three-attempt budget, independent 5-second
request gate, 20-second accepted-request settlement and 6-second idle grace
are unchanged. This bounds the number of calls per invocation, not the duration
of an individual synchronous stack call. It adds no thread or periodic idle work.

For a bounded measurement, add `CONFIG_ZMK_BLE_LINK_POLICY_DIAGNOSTICS=y`
alongside `CONFIG_ZMK_BLE_LINK_POLICY=y` and `CONFIG_LOG=y`; retain an INFO
log level (`CONFIG_ZMK_BLE_LINK_POLICY_LOG_LEVEL=3`). Diagnostics default off.
They add a four-byte sequence counter, temporary counters/cycle reads and one
numeric `linkwork v=1` log per completed invocation that issued a request.
Measure the linked flash/RAM and logging cost on the actual build. Durations
are wall microseconds, including preemption/blocking and excluding the final
summary log, not CPU time or radio occupancy. `invalid` flags ambiguous cycle
spans/overflow. `pending_ms=4294967295` means no deadline; `active_slot=255`
means no selected BLE host. `host_mask` records eligible known hosts seen in
the scan; a stale scan may be partial. A selected profile outside 0..4 is also
reported as `active_slot=255`. Sequence gaps also include no-call work.
See [the observation command and limits](tests/README.md). Compare only matching
firmware, hosts, selected routes and workloads; pair this with real host HID
arrivals before claiming an end-to-end improvement.

Storage is a fixed five-slot policy table, one spinlock and one delayed work
item. It retains no connection reference between scans, allocates no thread
or heap and leaves no timer when reconciliation is complete. A concurrent
endpoint wake can queue the still-running work without a cancellation window. Retries use the
existing system workqueue; numeric logs contain no addresses or payloads.
Flash/RAM impact depends on the linked build and logging. Moving the same
policy is not a measured speed or power improvement.

Check the actual selected config, compiled source paths and linked symbols.
Build every integrating keyboard role, including settings-reset. Remeasure
selected-host HID arrivals, profile/USB switching, all connected hosts,
refusal/reconnect, split and power with matching firmware. Host/kernel/build
passes do not establish those physical results.

See [tests/README.md](tests/README.md).

## Troubleshooting

- An undefined policy symbol usually means the module root was not selected.
  Check the actual generated `.config` and compiled source paths.
- A split peripheral deliberately cannot enable this option. The tested cores
  warn about the dependency and build with policy sources/symbols absent.
- USB selection deliberately leaves no active BLE host. Select the intended
  BLE HID output/profile using the keyboard's own controls.
- API acceptance does not mean negotiated values changed. Inspect parameter
  notifications or a host connection observer, and keep the request gate and
  peer's own update constraints separate from workqueue duration.
- No `linkwork` row means no completed request worker was observed. Check the
  default-off diagnostic option, INFO logging, console and boot identity;
  absence is not a zero-duration measurement.

## License

GPL-3.0-only; see [LICENSE](LICENSE).

## Optional split activity policy

The split policy is separate from the selected-host policy. Enable it only on
an outgoing split central with exactly one configured peer; the peripheral and
settings-reset images keep it disabled. It works independently of
`ZMK_BLE_LINK_POLICY`, Studio and Messenger. Keyboard configuration owns the
role and numeric preferences; this module owns reconciliation and direct tests.

```conf
CONFIG_ZMK_BLE_SPLIT_POLICY=y
CONFIG_ZMK_BLE_SPLIT_POLICY_IDLE_INTERVAL=24
# Existing ZMK split preferences remain the ACTIVE target, for example:
# CONFIG_ZMK_SPLIT_BLE_PREF_INT=6
# CONFIG_ZMK_SPLIT_BLE_PREF_LATENCY=30
# CONFIG_ZMK_SPLIT_BLE_PREF_TIMEOUT=400
```

Intervals use 1.25 ms units: 6 is 7.5 ms and 24 is 30 ms. A latency of 30 is an
event-skip count, not milliseconds. Timeout uses 10 ms units: 400 is 4 s. The
supported idle range is 7..48 and must exceed the active preference. Compilation
checks the supervision bound `timeout*8 > 2*idle_interval*(latency+1)`. No host/profile
preference, advertising, bonds, PHY, payload size or persistent setting changes.

ZMK ACTIVE uses the normal split preference; IDLE uses the configured longer
interval; SLEEP issues no more requests. Stored activity timeouts remain with
ZMK/the selected settings owner. The first necessary ACTIVE restoration gets
one immediate attempt past this module's 500 ms request-spacing gate. That gate
is an owner policy, not a core-imposed BLE delay. Idle requests and later
refused attempts retain the spacing. An already accepted procedure is not
cancelled: after its API call returns, the policy waits for matching observed
parameters or up to 5 s before reconsidering the newest activity intent. The
first ACTIVE exception does not bypass that accepted-procedure wait, and 5 s
does not bound the controller's actual application time. There are at most
three consecutive attempts without observed convergence; mode or
connection changes and convergence reset the budget. Matching applied values
or exhausted attempts leave no periodic idle work. API success is not proof
of applied parameters; a synchronous stack call can block independently of
these request-count and timer bounds.

Only connected LE central-role links on the default identity qualify. The
single-peer configuration is required, and multiple actual eligible links
fail closed. Host peripheral-role connections are excluded even when five
hosts coexist. The policies never share selection, generations or attempts.
Temporary connection references are released before the next worker pass.

No new thread, heap or persistence is added. One policy record, one delayable
work item and lock/generation/index are fixed storage. The selected 32-bit ARM
configuration uses 97 writable symbol bytes before alignment (40+48+8+1);
callbacks, logging metadata and code also use flash. Inspect the final linked
map for the actual configuration and runtime stack boundaries. Log level is
shared with `ZMK_BLE_LINK_POLICY_LOG_LEVEL`; split observations are numeric.

Build and install paired normal images through the integrating keyboard's
procedure. A settings/bond erase is not required. Remeasure actual parameters,
first input from both halves, host HID arrivals, reconnect, USB changes, LED
synchronization and current with matching installed hashes/boot identities.
The first split input travels on the currently negotiated link before any
restoration can be requested. Native 10 ms input-to-request assertions use a
controlled Bluetooth API at the central receive boundary; they do not bound
on-air negotiation, input travel to the central or host HID delivery.

The source/state fixtures migrated from `zmk-config-akkb46` retain their MIT
SPDX notices and original assertions. See `tests/migration.json`. Direct tests
and mutations now belong here; keyboards retain configuration/source/linkage
admission and physical composition checks.
