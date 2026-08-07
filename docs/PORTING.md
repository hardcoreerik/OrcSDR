# Porting RTL-SDRv4-ESP to ESP32 devices

## Goals

1. **Standalone driver** (`components/rtl_sdr_v4_esp`) usable without OrcSDR UI.
2. **OrcSDR app** (`apps/orcsdr-tab5`) consumes the component for radio UX.
3. **OrcLink** remains the control-plane project; it does not own this driver.

## Target matrix

| Target | USB host | Status |
|---|---|---|
| ESP32-P4 (Tab5, Waveshare P4 kits) | High-Speed | **Measured** reference |
| ESP32-S3 | Full-Speed OTG | Build-only until measured |
| ESP32-S2 | Full-Speed OTG | Build-only until measured |
| Classic ESP32 | No native HS host | **Out of scope** |

Never claim a target without device identity, procedure, and observed result.

## Extraction gates

### Gate 1 — Component skeleton (current)

- [x] Public C API header
- [x] IDF component CMake / Kconfig / idf_component.yml
- [x] Private clean-room transfer tables
- [x] install/uninstall lifecycle stubs

### Gate 2 — Behavior parity with Tab5 radio

Move from `apps/orcsdr-tab5/ui/main.cpp` into the component **without** UI/audio:

1. V4 identity filter and hot-plug
2. Interface claim / release
3. Expected-STALL init sequence
4. 960 kS/s sample-rate records
5. Final-tune template + PLL packing for custom Hz
6. Bulk IN pipeline, stop, cleanup
7. Metrics (bytes, min/max/mean, effective sps)

**Pass:** same serial evidence as Tab5 continuous KZEL/NOAA without M5Unified.

### Gate 3 — Dual-core friendly IQ delivery

- USB owner task only talks to USB Host API
- SPSC / FreeRTOS queue of IQ blocks to consumers
- Retune only when no bulk URB is outstanding

### Gate 4 — Second board

Repeat Gate 2 on Waveshare ESP32-P4 (or other measured P4) with board BSP for VBUS only.

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
| Daemon, policy, adapters (Windows host rtl-sdr, etc.) | RTL-SDRv4-ESP component |
| Firmware-test workflow for OrcLink node identity | Tab5 radio app / examples |
| Control Room, orclinkctl | Optional future OrcLink adapter *calling* OrcSDR |
