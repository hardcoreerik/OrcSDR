# OrcDial integration after current-main update

October 30, 2026 (America/Los_Angeles). PR #226 remains unmerged.

## Candidate and build

Current main `3af1f5ef` (Dead Air Environment chart merge #220) was merged into
the integration branch without conflicts or rewriting published history, producing
`069a44f555568023fd0e6943ab3ac38cb922c6c7`. Follow-up `d9b062fe` changes only host
tests/runners and CI filters; its firmware inputs are identical to the built image.

The native IDF 5.5.4 incremental build passed:

```text
ninja -C apps/orcsdr-tab5/build-native-hosted3 -j 2
```

Log: `.pio/sstv-dial/combined-native-build.log`. Application size 5,445,616 bytes;
13% partition free. Shared FT8/FT4/JS8 PCM scratch passed the PSRAM placement check.
Pinned application: `.pio/sstv-dial/combined-validation/tab5-combined-069a44f5.bin`.
SHA256: `a30d7d500a460390b42751d4795f28761ca9c45a485934fb38202eb7d1373f9c`.

Application-only flash on COM17 at 0x10000 passed `Hash of data verified`.
Bootloader, partition table, NVS and C6 firmware were not flashed. Previous
application `tab5-7b348ead.bin` was retained and its SHA256 verified before writing.
Flash and artifact records: `.pio/sstv-dial/combined-validation/flash.log` and
`firmware.json`. The first CLI invocation used unsupported hyphenated esptool
option names and was rejected before device access; its output is retained.

The Dial on COM14 was not flashed in this validation. This test uses its existing
companion implementation, not a new physical acceptance run of the tagged release.

## Focused host checks

- `python -m unittest discover -s tests -p test_ft8_dial_device.py -q`: 23/23 PASS.
  Includes diagnostic-loss verdicts, authentication redaction, missing-Git
  reporting, partial-port cleanup and independent Dial mute when tablet STOP fails.
- The imported `dead_air_envhistory_tests.cpp` and `dead_air_render_tests.cpp`
  were compiled/run using their `.github/workflows/dead-air-host-tests.yml`
  optimized warnings-as-errors commands: PASS.
- Previous fix commit `62293d7f` also passed the embedded Dial protocol test and
  optimized/ASan/UBSan delivery test. Those sources are unchanged by the main update.
- All GitHub CI jobs for `d9b062fe` passed. CodeRabbit had marked the original nine
  findings addressed and raised three follow-ups, fixed in `d9b062fe`: transitive
  header filters, missing-map INCONCLUSIVE and independent mute/off cleanup.
  Final external review status is tracked in the PR; a green CI job alone is not
  represented as a finding-free review.

## On-device check

The first attempt stopped before retuning: C6 transport was healthy, but tablet
bridge=0 and Dial offline. Normal authenticated `RTL_ORCDIAL_CONNECT` plus
`ORCDIAL_CONNECT` restored bridge=1, trust=1 and Connected without erasing trust
or re-pairing. Initial failure and recovery logs are preserved in
`.pio/sstv-dial/combined-validation/live-ft8/` and `connect.log`.
This does not establish automatic reconnect or cold-start acceptance.

Then ran:

```powershell
python tools/test_ft8_dial_device.py --tab5-port COM17 --dial-port COM14 --key F:/Ai/OrcSDR/.orclink/ui-doc.key --band 20m --seconds 90 --antenna "User-reported outdoor telescopic antenna set for 20m, 50-foot feedline; unchanged setup not physically rechecked" --dongle "Last user-reported V4l; not independently reidentified" --output .pio/sstv-dial/combined-validation/live-ft8-connected
```

**PASS, exit 0, 93.55 seconds measured.** C6 transport, existing pairing, exact
dashboard command acknowledgement and correlated live delivery passed.
Five measured slots contained 13, 12, 11, 12 and 11 real receiver decodes (59
total). Complete Dial summary/text records 13, 25, 36, 48 and 59 each mapped to
the corresponding measured slot through `ORC_FT8_DIAL_RECORD`. Errors: none.
No synthetic decode/audio was injected. FT8/IQ remained active at sampled checks.
Evidence: `live-ft8-connected/report.json` and its allowlisted `serial.log`, under
the combined-validation directory. Devices were left connected at 14.074 MHz.

No new physical sound, pixel, encoder/touch, SSTV on-air, long-duration or forced
reconnect acceptance is claimed by this FT8 test. No branch was merged into main
and no OrcSDR release was created.
