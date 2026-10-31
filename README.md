# ZMK BLE Link Policy

[日本語](README.ja.md)

Prioritize the selected BLE HID host while keeping other bonded host links
connected with peripheral latency 30. This independent module needs neither
Messenger nor Studio. It uses public ZMK endpoint/profile and Zephyr connection
APIs; no core edits, wrappers, heap, persistent settings or idle periodic work.
Both unchanged stock ZMK and DYA are supported.

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

### Migrating from Messenger

Use exactly one host-parameter policy. If your Messenger revision still declares
`ZMK_BLE_MESSENGER_IDLE_LINKS`, set `CONFIG_ZMK_BLE_MESSENGER_IDLE_LINKS=n`
before enabling this module. If that old symbol has been removed from your
Messenger revision, remove its assignment entirely; assigning an unknown symbol
can produce a Kconfig warning. Messenger traffic/HID-credit protection and BLE
Management profile labels/output controls remain independent features.

### Compatibility and tests

The integration matrix selects these unchanged commits:

| Core | Selected test revision |
| --- | --- |
| Stock ZMK | `5b51501fead672c41b5cfb396f3dafe0894bf4e9` |
| DYA | `e5c9b6915b56801193e359dd9bad4a167ce0d1b8` |
| Shared Zephyr | `10ba6d0cb38bc3d258775d27982f707599320085` |

[The complete test pins](tests/ci/pins.json) also fix CMSIS, Nordic HAL,
TinyCrypt, Mbed TLS, nanopb, the core-specific Studio schema and the SDK image digest.
Other core revisions need their own integration results. The module does not
depend on a sibling checkout, AKKB46 configuration or a private build cache.
See [reproducing CI](tests/README.md#reproducing-ci),
[the test contract](tests/maintenance.json) and
[validation and hardware acceptance](docs/validation.md). A hosted CI badge is not
provided before an actual hosted run.

## Behavior and ownership

The actual selected HID endpoint determines active mode. USB or no endpoint
means no active BLE HID host. Only connected known host profiles on the default
identity in the peripheral role are eligible; split-central links, guests,
other identities and disconnected objects are excluded. Application traffic,
INFO reads, Messenger admission and Studio locks do not promote a host.

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
owned here. HID buffer protection and Messenger discovery remain their owners'
separate integration. BLE Management still owns profile labels and DYA RPC.
The selected platform's own initial parameter update remains a platform action;
additional application policy providers must not independently fight this one.

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
the scan; a stale scan may be partial. Sequence gaps also include no-call work.
See [the observation command and limits](tests/README.md). Compare only matching
firmware, hosts, selected routes and workloads; pair this with real host HID
arrivals before claiming an end-to-end improvement.

Storage is a fixed five-slot policy table, one spinlock and one delayed work
item. It retains no connection reference between scans, allocates no thread
or heap and cancels its timer when reconciliation is complete. Retries use the
existing system workqueue; numeric logs contain no addresses or payloads.
Flash/RAM impact depends on the linked build and logging. Moving the same
policy is not a measured speed or power improvement.

Check the actual selected config, compiled source paths and linked symbols.
Build every integrating keyboard role, including settings-reset. Remeasure
selected-host HID arrivals, profile/USB switching, all connected hosts, Studio
and Messenger coexistence, refusal/reconnect, split and power with matching
firmware. Host/kernel/build passes do not establish those physical results.

See [tests/README.md](tests/README.md) and the [assertion migration map](tests/migration.json).

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

## License and origin

GPL-3.0-only; see [LICENSE](LICENSE). The policy and its original assertions
were extracted from ZMK BLE Messenger. [The migration map](tests/migration.json)
retains ownership/provenance and the composition cases that stay with Messenger.
