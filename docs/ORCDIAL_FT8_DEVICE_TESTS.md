# Automated FT8 companion device checks

From the OrcSDR checkout, with pyserial installed and both serial monitors closed:

```powershell
python tools/test_ft8_dial_device.py --tab5-port COM17 --dial-port COM14 --band 20m --seconds 120 --png --antenna "Outdoor telescopic antenna set for 20m; 50-foot cable; suitability user-reported" --dongle V4l
```

The existing serial authorization key is found through the current helper's
`--key`, `ORC_UI_DOC_KEY`, local `.orclink/ui-doc.key`, or main-checkout fallback.
No key or authentication proof is printed or written into the evidence log.

The runner checks C6 transport health and existing pairing, then (with `--band`)
selects FT8 and the requested band and verifies a Dial dashboard command's exact
transmit/ack sequence. It watches receiver decode events and new Dial record IDs,
requiring a summary followed by matching text. Cached cards, retries, mismatched
IDs and text arriving before its summary do not count as new live delivery.
The receiver emits `ORC_FT8_DIAL_RECORD record=<id> slot=<utc>` when it creates
each preview. PASS requires a complete Dial record whose receiver mapping names
a measured slot with actual decodes. Older firmware without this diagnostic
returns INCONCLUSIVE when correlation cannot be proved. Earlier recorded runs
used the prior runner; they have not been repeated under this stricter check.
The receiver store being full does not prevent detecting fresh slot decodes.

`--png` uses the existing hash/length-checked `FT8 SNAP` helper before and after
observation, then checks that FT8/IQ is still active. It never uses the SD capture
fallback, stops IQ for capture, or silently reclaims memory. Insufficient PNG
workspace produces a recorded capture failure; inactive caches can be released
separately through the existing safe memory-reclaim command if needed.

Omit `--band` to observe the current setup without tuning or reopening the
dashboard; the command-ack check is then explicitly NOT_RUN. Gain is never set
by the runner. An explicitly selected band is left active afterward. Selecting
it is a normal retune and can discard a partial FT8 slot during setup.

`--reconnect` explicitly exercises Dial Disconnect/Connect without erasing trust.
It polls asynchronous status transitions and attempts ordinary Connect cleanup
if interrupted. **Use this separately from the normal receive test:** the first
hardware attempt in this session ended with C6 transport offline, and normal
Connect could not restore it. That path is not hardware accepted. The corrected
polling logic has unit coverage but reconnect stress was not repeated after
recovery. The runner never automatically restarts a board, flashes, or re-pairs.

Each run creates a new `.pio/ft8-dial-device/<UTC timestamp>/` directory (or an
explicit new `--output` directory). It contains an allowlisted serial log,
`report.json`, optional before/after PNGs, artifact hashes, setup description,
host repository revision and runner source hash. The host repository revision
is not a device firmware identity; use the separate flash-evidence ledger for
installed application hashes. Ports are always closed afterward.

| Exit | Verdict | Meaning |
|---:|---|---|
| 0 | PASS | Required observed connection/delivery checks and requested PNG transfers passed |
| 1 | FAIL | Missing/broken connection, stopped IQ, changed setup, missing delivery despite new source decodes, or command/capture error |
| 2 | INCONCLUSIVE | No new RF decode, invalid clock, or missing record/slot diagnostics; live correlation is not proved |

Observe long enough for multiple complete 15-second FT8 slots. A quiet daytime
60 m run can be inconclusive while 20 m passes. Preserve antenna, feedline,
dongle, gain and time context when comparing bands or day/night reception.
Nothing synthetic is injected. This runner does not claim per-field payload
readback, physical image quality, selected-history projection, multi-station
mapping, forced packet loss or legacy-firmware coverage.

## Focused host validation and CI

```text
python -m unittest discover -s tests -p test_ft8_dial_device.py -v
```

The small `FT8 Dial device-runner logic` workflow runs only these host tests;
GitHub-hosted CI does not access physical devices. No firmware rebuild is needed
for this Python-only change.

## October 30 implementation evidence

- Fifteen focused unit tests passed, including quiet RF, cached/mismatched
  records, full decode store, clock loss, C6/link failures, sequence rollover,
  partial serial reads, authentication-log filtering and async status polling.
- Initial hardware run `20261030T172618019511Z`: dashboard acknowledgement passed,
  then the first runner version queried Disconnect too early and missed the
  asynchronous status change. The corrected runner polls read-only status
  queries; mutating commands are never automatically repeated.
- Subsequent status probes showed C6 `transport=0`, companion `bridge=0`, and
  preserved trust on both devices. Ordinary Connect and saved-Wi-Fi Connect did
  not recover the link. One controlled Tab5 restart restored `transport=1`,
  `bridge=1`, and Dial LINKED without re-pairing or writing firmware. Cause of the
  C6 loss is not established. The restart reset volatile gain to the boot setting;
  the runner itself did not issue gain commands.
- Normal hardware run `20261030T173419202355Z`: **PASS**, measured 63.63 seconds,
  25 decodes across observed fresh slots and complete new records 7, 17 and 25.
  Existing pairing, C6, exact dashboard acknowledgement, live delivery and both
  PNG transfers passed. No reconnect stress was attempted in that passing run.
- Final runner run `20261030T173711841211Z`: **PASS**, 33.57 seconds, complete
  new records 88, 93 and 99, with both PNG transfers passing. This run omitted
  `--band`, so no tuning/dashboard command was sent and control acknowledgement
  was explicitly NOT_RUN. It validates the final bulk/partial-read handling.
- The failed report and `recovery.log` remain saved; passing receive checks do
  not turn the reconnect case into a pass. The paired devices are connected
  on 20 m again.

These local run directories are evidence artifacts, not release assets. No
firmware source changed, no devices were flashed, and nothing was merged.
