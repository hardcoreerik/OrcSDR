# Weather Dashboard Foundation acceptance status — 2026-10-07

Candidate branch: `Codex/weather-foundation`  
Draft PR: #173

## Verified in software

- Pure source/freshness model has host tests.
- NOAA channel model covers exactly seven channels from 162.400 through 162.550 MHz.
- Weather runtime host test proves dashboard entry emits no receiver command.
- NOAA scan sequencing/strongest-result behavior has host coverage.
- Weather receiver borrowing cannot replace an already-owned receiver session.
- Weather screen ownership/layout self-checks and report-format tests are wired into the radio-scan host suite.
- Weather NOAA scan integration uses the existing shared `scan_engine`; no second DSP/audio/demodulator path was added.
- Online policy defaults to disabled.

## Not hardware verified in this change

No Tab5 was flashed or reset during this implementation session. Therefore the following remain **Not Verified** for this candidate until an explicit COM17 hardware run is recorded:

- physical 1280x720 rendering and touch targets;
- live NOAA NFM audio;
- seven-channel live RF scan and strongest-channel comparison;
- receiver takeover/stop behavior on physical hardware;
- SD report creation/removal/recovery on the Tab5;
- OrcDial interaction with the Weather screen;
- long-duration USB/DSP/audio stability.

No claim of Hardware-Verified or RF-Verified is made by this document.

## SAME boundary

Current `main` contains the WX/NFM receive path but no NOAA SAME decoder implementation. SAME decoding remains in the separate Weather Hunter plan and is not represented as implemented by Foundation.
