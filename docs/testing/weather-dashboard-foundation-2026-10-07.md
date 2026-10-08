# Weather Dashboard Foundation test report — 2026-10-07

## Candidate

- Branch: `Codex/weather-dashboard-foundation`
- Draft PR: #174
- Base: `0b8be74ecfa58a315d07210f953f0fd5626943e2` (post-PR #170 Smart VFO Home main)
- Scope: Phase 1 Foundation only. Weather Hunter and Satellite Weather plans are out of scope.

## Implemented checks

- Pure host tests cover Weather source/freshness behavior, the exact seven NOAA channel centers, sequential scan accumulation/strongest selection, report escaping/format/provenance age, and receiver ownership.
- Existing optimized and ASan/UBSan Radio scan suite remains the owning regression suite for `radio_session` / `scan_engine` changes.
- The authenticated UI harness has `RTL_UI OPEN WEATHER` plus Weather `TAB`, `LISTEN`, `SCAN`, `STOP`, `REPORT`, `POLICY`, `SETTINGS`, and `HOME` actions.
- The canonical Tab5 UI regression now records the active band/frequency, opens Weather, changes one Weather tab, and fails if opening Weather retunes the receiver.

## CI evidence

- Radio scan core: PASS on Foundation code; current-head re-runs are required after documentation-only closeout commits.
- Documentation Truth: pending final current-head pass after architecture and user-guide updates.
- Native ESP-IDF 5.5.4 firmware build / app-size comparison: pending the PR-only build comparison workflow. Earlier workflow failures were harness setup failures (missing `jq`, then an overlong test build ID) before Tab5 compilation.

## Hardware and RF evidence

- **Tab5 flash/run: NOT RUN.** This work has not flashed, reset, or rebooted COM17.
- **NOAA RF/audio acceptance: NOT RUN.** No physical dongle/antenna/channel run is claimed.
- **Touch/layout acceptance on physical display: NOT RUN.** M5GFX code is implemented; physical display evidence remains open.
- **SD report bundle on physical microSD: NOT RUN.** Serialization/transaction behavior is implemented; device SD acceptance remains open.

Those boundaries mean this report may use `Implemented` and `Regression-Tested` where supported, but it does not use `Hardware-Verified` or `RF-Verified` for Weather Foundation.

## Known deliberate limitations

- NOAA SAME decoding is **not implemented**. Current `main` had no existing SAME decoder to reuse, so Foundation does not claim one.
- No Internet weather/forecast/radar provider is implemented. The network policy is persisted with default `DISABLED`; changing the policy does not start a network request.
- Weather Hunter, personal weather-station decoding, radiosonde/Balloon Hunter, satellite pass prediction, and LRPT decoding remain later phases.
- NOAA Scan is bounded discovery: after seven channel samples it caches the strongest relative-dBFS result and stops Weather RF. Continuous listening requires explicit `LISTEN`.
- Weather report save is refused while Weather RF is active.

## Planned physical acceptance commands

After warning the operator before any flash/reboot and using **COM17 explicitly** (never auto-detect):

```text
RTL_UI OPEN WEATHER
RTL_UI ACTION WEATHER TAB 3
RTL_UI ACTION WEATHER SCAN
RTL_UI ACTION WEATHER LISTEN
RTL_UI ACTION WEATHER STOP
RTL_UI ACTION WEATHER TAB 4
RTL_UI ACTION WEATHER REPORT
```

The non-destructive UI regression also includes the Weather-open no-retune check in `apps/orcsdr-tab5/tools/run-tab5-ui-regression.ps1`.
