# Weather dashboard changelog

## 2026-10-07 — Foundation

- Added a dedicated five-tab 1280×720 Weather dashboard that opens without retuning the receiver.
- Added source/freshness metadata and an offline-first network policy whose default is disabled.
- Added the seven standard U.S. NOAA Weather Radio centers as one authoritative channel plan.
- Added explicit NOAA WX/NFM Listen and bounded sequential Scan actions using the existing receiver/DSP/audio path and shared scan engine.
- Added a dedicated Weather receiver owner while preserving the legacy `Band::wx -> Owner::radio` mapping for older callers.
- Added OrcMaps/location and signed `noaa_weather` catalog status to Weather.
- Added SD Weather report/history bundles with JSON, CSV, HTML, RF events, alert metadata and SHA-256 manifests; report serialization uses PSRAM and is blocked while Weather RF is active.
- Added authenticated serial Weather actions and a no-retune Weather-open regression to the Tab5 UI test harness.
- Added host regression tests for Weather model/freshness, NOAA scan sequencing, report serialization and receiver ownership.
- Explicitly did **not** add NOAA SAME decoding, personal-weather-station decoding, radiosonde/Balloon Hunter or satellite Weather; those remain later phases.
