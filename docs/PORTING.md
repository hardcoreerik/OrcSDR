# Porting `esp_rtl_sdr` to ESP32 devices

> **Current OrcSDR dependency (2026-09-12):** driver 0.8.0-rc2 at immutable
> pin `b175dfea6782faa97e512d4a2408767c75977527`. Older pins below are retained
> as Historical Evidence for the integration that was actually tested then.

## Goals

1. **Standalone driver** ([`hardcoreerik/esp-rtl-sdr`](https://github.com/hardcoreerik/esp-rtl-sdr)) usable without OrcSDR UI.
2. **OrcSDR app** (`apps/orcsdr-tab5`) consumes the component for radio UX.
3. **OrcLink** remains the control-plane project; it does not own this driver.

## Existing implementation and evidence

The standalone driver extraction is complete. OrcSDR pins `esp_rtl_sdr`
directly to reviewed Git commits; its public C API, USB/tuner implementation,
tests, and `p4_serial_smoke` example live in the driver repository. See the
[integration contract](API_ESP_RTL_SDR.md).

### Blog V4 HF-routing integration (2026-09-09)

The Tab5 application moved from driver v0.7.14 commit
`69e61848008ed0fce5a823ad370aee5cb81d65da` to v0.7.15 commit
`1cd19d1363daea49b013c2d28a25750fcbfcff78` from branch
`codex/v0.7.15-v4-hf-routing`. The exact commit pin is intentional while the
driver change awaits release tagging.

The driver change completes the capture-derived Blog V4 HF route: R828D
Cable-2 selection and RTL2832 GPIO5 upconverter switching are composed with
VHF/UHF restoration, manual/AUTO gain state, and Bias-T GPIO0. The previous
driver already translated RF below 28.8 MHz to the tuner frequency; the defect
was the incomplete physical RF route.

Upstream source evidence reports 372 host assertions passed and a successful
ESP32-P4 smoke build. OrcSDR's native ESP-IDF 5.5.4 build also passed with the
new managed component compiled from the exact commit. No firmware was flashed
for this integration. GPIO5/Bias-T behavior, raw AM and Shortwave RF, AM audio,
and VHF/UHF restoration remain separate physical acceptance gates.

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

The in-tree P25 Phase I C4FM receiver and IMBE-to-PCM processor are now
hardware-independent modules. A new board can feed CU8 IQ and a monotonic
timestamp into `p25_decoder_core`, then consume bounded voice frames and PCM
without linking M5Unified or the Tab5 display. Tuner ownership, task creation,
storage, and audio-device delivery remain board-application responsibilities;
the current reference adapter is `apps/orcsdr-tab5/ui/p25_decoder.cpp`.

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
