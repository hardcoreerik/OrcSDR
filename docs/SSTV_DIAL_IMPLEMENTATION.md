# SSTV Dial companion: implementation and device evidence

October 30, 2026. Extends the existing paired FT8 companion with SSTV
Receive/Tune and Dial sound controls. No RF transmit capability was added.

The tablet publishes actual decoder session, mode, progress, completed-image
count and archive outcomes through the existing authenticated RADIO_STATE
packet, dashboard 18, schema bit 30. Contract: `orcdial/src/sstv_control.hpp`;
producer: `apps/orcsdr-tab5/ui/sstv_dial_state.hpp`. Old peers without this schema show
unavailable controls. Save, abort, gain, mode, step and tuning reuse the tablet
handlers; tuning and mode changes are guarded during image reception.

`SSTV DETECT AUTO|VIS` now exposes the existing detection preference through
authenticated serial commands and the `SSTV_TUNE` line of `SSTV STATUS` reports detection/manual mode.
It refuses changes during reception. The bench explicitly selects AUTO:
a saved manual Scottie S1 filter correctly prevented the first Robot36 fixture
from starting. That failed attempt is retained, not counted as a passing test.

Dial controls are described in OrcDial's `docs/orcdial-2/SSTV_AND_SOUND.md`.
Receive shows actual SD ready/pending/saved/failed state; SAVE and ABORT have
two-step, four-second confirmation tied to the current session. Short alerts
default off, persist their settings and support immediate master mute/stop.

## Installed applications

- Tab5 COM17: source `7b348ead`, 5,440,256 bytes, SHA256
  `a311a05687b09f5506362a50ce2a3697c50feca2ba791c7cb5ea1a054793b9c7`.
  Native IDF build passed, including FT8 scratch PSRAM placement; 14% partition
  space remains. `.pio/sstv-dial/tab5-final-firmware.json` and
  `tab5-final-flash.log` retain evidence.
- Dial COM14: source `298fa94`, 1,152,960 bytes, SHA256
  `3274017778e9a3c6a14dd81404a769c3d7b3f8fed1b52c574b77a1316710ef8c`.
  Build/flash evidence is in the Dial checkout's
  `.pio/hardware-slice1/sstv-sound/`.

Both application-only flashes at 0x10000 passed hash verification. Bootloader,
partition table, NVS and C6 firmware were preserved. The trusted link resumed.
Previous known installed images remain pinned locally for rollback.

## Repeatable paired check

From this checkout, with both serial ports free:

```powershell
python tools/test_sstv_dial_device.py --tab5-port COM17 --dial-port COM14 --key F:/Ai/OrcSDR/.orclink/ui-doc.key --frequency 14230000 --out .pio/sstv-dial/new-run
```

This opens SSTV, selects automatic detection, retunes to the explicitly supplied
frequency, enables Dial alerts, injects internal Robot36 at speed 2, checks new
receive progress/completion, verifies retune protection and confirms an SD save.
It requires an SD card and paired devices. Output must be a new directory.
It writes a log and JSON result, exits nonzero on failure, stops injection and
leaves sound muted/off. It does not flash/reset hardware or transmit RF.
It changes the detection/frequency settings; it does not restore the prior band.
The firmware fixture temporarily replaces decoder audio, so this is not an
uninterrupted live RF acceptance test. `tools/sstv_device_bench.py` remains the
separate single-tablet decoder bench.

## Checks performed

- `bash tools/sstv_host_tests.sh` from `.pio/sstv-dial` using the relative script
  path `../../tools/sstv_host_tests.sh`: existing targeted SSTV suite passed.
  Real WAV replay skipped because no fixture was supplied. Summary:
  `.pio/sstv-dial/sstv-summary-20261030-133915.log`.
- `tests/sstv_dial_state_test.cpp`: optimized and ASan/UBSan checks passed.
- Dial CTest: 11/11 passed; production draw-function recording regression passed.
  These checks do not prove physical pixels/flicker or mechanical interaction.
- Hardware controls: tune/step acknowledgement, sound test/disabled event,
  master mute and stop driver state passed. Evidence:
  `.pio/sstv-dial/controls-pass-auth-timeout.log`. Its later fixture stage timed
  out on serial authentication; the subsequent reception run re-authenticated
  each state-changing tablet command and passed.
- `.pio/sstv-dial/device-test.log`: internal Robot36 progressed to 240 lines,
  complete-image counter advanced, SD auto-save and confirmed manual save
  advanced the saved counter, and tuning was blocked during reception.
- `.pio/sstv-dial/abort-test.log`: first abort press preserved reception;
  confirmed abort ended the same session and `SSTV LAST` reported ABORTED,
  partial=1. Injection was stopped before confirmation to prevent a subsequent
  fixture transmission from immediately starting another session.
- Earlier attempts and stale-query/authentication failures are retained under
  `.pio/sstv-dial/`; they are not represented as passes.
- Final installed-firmware run:
  `python tools/test_sstv_dial_device.py --tab5-port COM17 --dial-port COM14 --key F:/Ai/OrcSDR/.orclink/ui-doc.key --frequency 14230000 --out .pio/sstv-dial/final-paired-bench`:
  PASS (exit 0). Paired, retune blocked, new progress/completion and confirmed
  save all passed. Completed count advanced to 2, saved count to 7, failed=0.
  Log and JSON are retained in that output directory. Injection stopped;
  alerts enabled=0, muted=1, pending=0, active=0, driver_playing=0.

Over-the-air SSTV reception, physical sound quality, touch/encoder feel and
native pixel readability remain acceptance gates. No new RF propagation claim
follows from the internal fixture results. No main branch merge was performed.
