# Porting `esp_rtl_sdr` to ESP32 devices

## Goals

1. **Standalone driver** ([`hardcoreerik/esp-rtl-sdr`](https://github.com/hardcoreerik/esp-rtl-sdr)) usable without OrcSDR UI.
2. **OrcSDR app** (`apps/orcsdr-tab5`) consumes the component for radio UX.
3. **OrcLink** remains the control-plane project; it does not own this driver.

## Existing implementation and evidence

The standalone driver extraction is complete. OrcSDR pins `esp_rtl_sdr`
v0.7.9; its public C API, USB/tuner implementation, tests, and
[`p4_serial_smoke` example](https://github.com/hardcoreerik/esp-rtl-sdr/tree/v0.7.9/examples/p4_serial_smoke)
live in the driver repository. See the [integration contract](API_ESP_RTL_SDR.md).

Waveshare second-board work has already been performed under OrcSDR. The
driver's [validation provenance](https://github.com/hardcoreerik/esp-rtl-sdr/blob/9bd59129622e0b978b6a4b1fe748cf37fcc2bc37/docs/AI_DEVELOPMENT_DISCLOSURE.md#provenance-honesty-orcsdr--this-repo)
records the same tables working unmodified on Waveshare, and its
[lab inventory](https://github.com/hardcoreerik/esp-rtl-sdr/blob/9bd59129622e0b978b6a4b1fe748cf37fcc2bc37/docs/TESTING.md#hosts-sdr-under-test)
identifies the ESP32-P4 Module-DEV-KIT. Its
[FM application notes](https://github.com/hardcoreerik/esp-rtl-sdr/blob/9bd59129622e0b978b6a4b1fe748cf37fcc2bc37/docs/DRIVER_GAPS_VS_DESKTOP.md)
also describe on-device demodulation and web PCM output.

These records establish prior second-board operation, not completion of every
later release's soak/recovery gates. The Waveshare application source and raw
logs are not included in this checkout. Whether it shares the Tab5 DSP
implementation must be established from that source before planning a new
extraction or duplicate example.

## Target matrix

| Target | USB host | Status |
|---|---|---|
| ESP32-P4 M5Stack Tab5 | High-Speed | Reference consumer; see [project status](../PROJECT_STATUS.md) for version-specific evidence |
| ESP32-P4 Waveshare Module-DEV-KIT | High-Speed | Prior OrcSDR operation recorded upstream; see evidence above |
| ESP32-S3 | Full-Speed OTG | Streaming support not claimed; requires target-specific build and hardware evidence |
| ESP32-S2 | Full-Speed OTG | Streaming support not claimed; requires target-specific build and hardware evidence |
| Classic ESP32 | No native HS host | **Out of scope** |

Never claim a target without device identity, procedure, and observed result.

## Extraction milestones and remaining validation

The milestones below originated in the in-tree driver plan. Driver extraction
and board operation are distinct from extracting a reusable OrcSDR DSP engine;
see [architecture](../architecture.md#driver-dsp-and-board-boundaries).

### Gate 1 — Component skeleton (**complete**)

- [x] Public C API header
- [x] IDF component CMake / Kconfig / idf_component.yml
- [x] Private clean-room transfer tables
- [x] install/uninstall lifecycle

### Gate 2 — USB/tuner extraction (**implemented**)

The standalone component implements these responsibilities without UI/audio:

1. [x] V4 identity filter and hot-plug events
2. [x] Interface claim / release
3. [x] Expected-STALL init sequence
4. [x] 960 kS/s sample-rate records
5. [x] Final-tune template + PLL packing for custom Hz
6. [x] Bulk IN pipeline, stop, cleanup
7. [x] Metrics (bytes, min/max/mean, effective sps)

**Separate acceptance gate:** record the exact driver version, board, and
configuration for a five-minute serial smoke outside M5Unified at at least
95% effective sample rate, zero fatal USB errors, and bounded drop counts.
Prior board operation does not automatically close this version-specific gate.

### Gate 3 — Dual-core friendly IQ delivery (**implemented; recovery pending**)

- [x] USB owner task only talks to USB Host API
- [x] Driver supports IQ delivery; Tab5 selects callback-only delivery and copies borrowed data into its app-owned queue
- [x] Retune drains bulk before EP0
- [ ] Unplug/replug recovery without reboot on Tab5 and a second P4 board

### Gate 4 — Second-board operation (**recorded upstream**)

Waveshare Module-DEV-KIT operation is recorded above. Preserve that milestone;
track sustained-rate, unplug/replug, and release-specific regression results
separately instead of restarting board bring-up as unimplemented work.

## Board BSP boundary

The driver must **not** hard-code M5 Tab5 power rails. Apps provide:

- VBUS enable (e.g. `M5.Power.setExtOutput`)
- Optional status LEDs
- USB Host install if shared with other class drivers

## Clean-room rules

See `RTL_SDR_V4_CLEAN_ROOM_SPEC.md`. Do not consult or copy librtlsdr while
implementing transfer sequences. Public R820T2 register math used for derived
tunes must stay labeled.

## OrcLink relationship

| Lives in OrcLink | Lives in OrcSDR |
|---|---|
| Daemon, policy, adapters (Windows host rtl-sdr, etc.) | Version-pinned `esp_rtl_sdr` dependency |
| Firmware-test workflow for OrcLink node identity | Tab5 radio consumer integration |
| Control Room, orclinkctl | Optional future OrcLink adapter *calling* OrcSDR |
