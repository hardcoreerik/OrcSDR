# Porting RTL-SDRv4-ESP to ESP32 devices

## Goals

1. **Standalone driver** (`components/rtl_sdr_v4_esp`) usable without OrcSDR UI.
2. **OrcSDR apps** consume the component for radio UX (Tab5 native UI; Waveshare
   web/LCD shell).
3. **OrcLink** remains the control-plane project; it does not own this driver.

## Target matrix

| Target | USB host | Status |
|---|---|---|
| ESP32-P4 **M5Stack Tab5** | High-Speed | **Measured** product reference |
| ESP32-P4 **Waveshare Module-DEV-KIT** | High-Speed | **Measured** second board (driver unmodified) |
| ESP32-S3 | Full-Speed OTG | Build-only until measured |
| ESP32-S2 | Full-Speed OTG | Build-only until measured |
| Classic ESP32 | No native HS host | **Out of scope** |

Never claim a target without device identity, procedure, and observed result.

## Extraction gates

### Gate 1 — Component skeleton (**complete**)

- [x] Public C API header
- [x] IDF component CMake / Kconfig / idf_component.yml
- [x] Private clean-room transfer tables
- [x] install/uninstall lifecycle

### Gate 2 — Behavior parity with Tab5 radio (**implemented; soak pending**)

Move from `apps/orcsdr-tab5/ui/main.cpp` into the component **without** UI/audio:

1. [x] V4 identity filter and hot-plug events
2. [x] Interface claim / release
3. [x] Expected-STALL init sequence
4. [x] 960 kS/s sample-rate records
5. [x] Final-tune template + PLL packing for custom Hz
6. [x] Bulk IN pipeline, stop, cleanup
7. [x] Metrics (bytes, min/max/mean, effective sps)

**Remaining pass evidence:** five-minute serial smoke outside M5Unified at at
least 95% effective sample rate, zero fatal USB errors, and bounded drop counts.

### Gate 3 — Dual-core friendly IQ delivery (**implemented; recovery pending**)

- [x] USB owner task only talks to USB Host API
- [x] FreeRTOS queue of IQ blocks to consumers
- [x] Retune drains bulk before EP0
- [ ] Unplug/replug recovery without reboot on Tab5 and a second P4 board

### Gate 4 — Second board (**complete for core RF: Waveshare P4**)

Repeat Gate 2 RF path on a second measured ESP32-P4. **Driver source was not
modified.** Application BSP only (USB port choice, display, Ethernet UI).

| Item | Evidence |
|---|---|
| Board | Waveshare ESP32-P4-Module-DEV-KIT, P4 rev 1.3, MAC `80:f1:b2:d1:e0:d5` |
| App | `apps/orcsdr-waveshare` |
| Driver | `rtl_sdr_v4_esp` **v0.4.1 unchanged** |
| Enumerate | Blog V4 HS `0bda:2838` |
| ADS-B | 1090 MHz @ 2.048 MS/s; live CRC-valid Mode-S / aircraft |
| FM | 960 kS/s stream; on-device demod + browser PCM |
| USB host port | **Lower-left Type-A next to Ethernet** (measured) |
| Report | [`WAVESHARE_P4_VALIDATION.md`](WAVESHARE_P4_VALIDATION.md) |

No Tab5 `M5.Power` VBUS toggle on Waveshare — host USB-A is used as-is.
OTG jumper must be **HOST**.

**Still open after Gate 4:** formal five-minute soak artifact, hot-plug recovery
on both boards, S2/S3 measurement.

## Board BSP boundary

The driver must **not** hard-code M5 Tab5 power rails. Apps provide:

- VBUS enable when required (e.g. `M5.Power.setExtOutput` on Tab5)
- Optional status LEDs
- USB Host install if shared with other class drivers
- Board-specific display / network / audio codec wiring

Waveshare proof: same install/start/retune/stop API as Tab5; zero driver edits.

## Clean-room rules

See `RTL_SDR_V4_CLEAN_ROOM_SPEC.md`. Do not consult or copy librtlsdr while
implementing transfer sequences. Public R820T2 register math used for derived
tunes must stay labeled.

## OrcLink relationship

| Lives in OrcLink | Lives in OrcSDR |
|---|---|
| Daemon, policy, adapters (Windows host rtl-sdr, etc.) | RTL-SDRv4-ESP component |
| Firmware-test workflow for OrcLink node identity | Tab5 radio app / Waveshare shell / examples |
| Control Room, orclinkctl | Optional future OrcLink adapter *calling* OrcSDR |
| Waveshare display pin map (ILI9341) used as reference | Ported into `apps/orcsdr-waveshare` only |
