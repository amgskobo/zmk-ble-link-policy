# Validation and hardware acceptance

The owner workflow runs the complete portable, real-kernel and firmware-selection
matrix using [the fixed CI graph](../tests/ci/pins.json). Local execution and a
hosted GitHub run are separate results. This guide contains no individual
device identities, physical capture histories or private build measurements.

## Reproduce software validation

Use [the container procedure](../tests/README.md#reproducing-ci) to prepare the
selected dependencies explicitly and execute all five lanes: host, native,
split (including both real activity cores), unchanged stock ZMK integration
and unchanged DYA integration. Retain commands,
actual exits, per-case results, image and dependency identities, and source
hashes before and after each lane. Required repeats, negative controls,
sanitizers and role variants remain part of those owner entries.

Portable and kernel fixtures use synthetic events and controlled Bluetooth
boundaries. Firmware selection checks inspect the real generated configuration,
compiled sources and symbols. These results do not establish that a host applied
a requested parameter, that HID arrived, or that radio duty or power changed.

## Hardware acceptance procedure

For a separately authorized integrating-keyboard build, retain exact source,
configuration, ELF/firmware hashes and boot identity in private records. Identify
the selected HID route and the complete host/split/service topology. Never mix
observations from different boots, images or parameter policies.

Exercise selected-host promotion, deselection and return; USB/BLE switching;
all five known hosts; independent refusal/retry and reconnect; split delivery;
and quiet completion. Observe actual negotiated interval, latency, supervision
timeout and PHY separately from requested values.
For the optional split policy, include ACTIVE/IDLE/SLEEP transitions, input from
both halves, a refused restoration, an old accepted IDLE procedure completing
after new input, and disconnect/reconnect. Keep the 500ms retry spacing and
5s accepted-procedure observation wait separate from measured radio negotiation
and the first report's travel to the central.
Pair ordered device observations with actual host HID arrivals before assessing
transition latency, loss or an end-to-end improvement. Measure radio and power
with suitable hardware rather than deriving them from workqueue durations.

The optional numeric diagnostics can be analyzed with:

```sh
python3 tests/observations.py --console CONSOLE.jsonl --output NEW_OUTPUT
```

Keep the input console and device matching details private. The analyzer preserves
ordered rows, boot/capture identity, invalid timing, stale scans and sequence
gaps. Completed-worker wall time includes blocking/preemption and excludes the
final summary log; it is not CPU time, RF occupancy or HID latency. Resolution
comes from the selected cycle clock. Missing rows and valid zero values remain
distinct observations.

Keep raw console captures and image/device matching records as private originals.
Before sharing a selected summary, inspect its text, filenames and metadata for
credentials, peer/host/device identities and private paths. Use clearly synthetic
public examples; retain public project identifiers, API names and attribution.
Review generated artifacts separately. State exactly
which scenarios ran and retain FAIL, BLOCKED and NOT RUN where applicable.
A successful read, local software matrix or hosted badge does not replace
physical USB/BLE/split/reset, retention, timing or power acceptance.
