# OrcSDR current architecture

This describes `main` at
`c5b3423fbdeb129ebd63d8ffd5c1bced9c913c04` on 2026-09-12. Historical plans
and validation reports explain how the design arrived here but are not current
architecture contracts.

## Platform and dependency boundary

OrcSDR's production Tab5 firmware is a native ESP-IDF 5.5.4 application for
ESP32-P4. The onboard ESP32-C6 runs matching ESP-Hosted 3.0.6 firmware and uses
4-bit SDIO at the qualified 10 MHz clock. PlatformIO configurations are
historical and unsupported.

The portable receiver boundary is the external `esp-rtl-sdr` component,
0.8.0-rc2, pinned immutably at
`b175dfea6782faa97e512d4a2408767c75977527` by
`apps/orcsdr-tab5/main/idf_component.yml` and
`apps/orcsdr-tab5/dependencies.lock`. OrcSDR uses callback delivery and owns
DSP, UI, storage, and product behavior above that driver. The old local USB
implementation is forced off, although disabled source blocks remain.

## Application ownership

`apps/orcsdr-tab5/ui/main.cpp` remains the top-level application and still owns
substantial cross-cutting behavior: startup and tasks; receiver lifecycle and
hotplug; tuning and stream ownership; generic band routing; FM, AM, WX, CB and
Browse DSP/audio policy; RDS; P25, ADS-B, LoRa and POCSAG runtime integration;
Wi-Fi pause/resume orchestration; catalog operations; serial and authenticated
device commands; SD/IQ/audio transfers; LAN console command dispatch;
documentation capture; screen transitions; and top-level touch routing.

main.cpp measurement (Git-normalized): 713,890 bytes (~697.2 KiB), 15,751 lines.

The measurement uses LF-normalized repository bytes so it is stable across
Windows and Linux checkouts. The intended modular endpoint—roughly 500 lines of
application wiring, setup, loop, and task creation—has not been reached. This
documentation task does not refactor it.

## Display and dashboard ownership

`screen_controller` is the single framebuffer owner. A transition grants one
screen permission to draw while radio/decoder work continues independently.
`navigation_service` owns Home/Settings handoff mechanics; feature dashboards
render snapshots rather than owning receiver state.

ScreenController IDs: `none`, `home`, `fm`, `p25`, `adsb`, `lora`, `radio`, `visualizer`, `rf_lab`, `wifi_analysis`, `pocsag`, `settings`, `am`, `documentation`.

Dashboard IDs: `home`, `fm`, `p25`, `adsb`, `shortwave`, `weather`, `cb`, `lora`, `airband`, `marine`, `satellite`, `utilities`, `settings`, `rf_lab`, `wifi_analysis`, `pocsag`, `am`.

The current screen modules include Home, FM, AM, P25, ADS-B, LoRa, POCSAG, RF
Lab, RF Visualizer, Wi-Fi analysis, Settings, documentation capture, and the
shared Radio/Scope/Capture surface. Dashboard catalog entries for Shortwave,
Airband, Marine, and Satellite route into that shared receiver surface; catalog
labels do not imply dedicated decoders or complete demodulation modes.

## Receiver and DSP ownership

- `radio_session` serializes receiver ownership and generation changes.
- `scan_engine` supplies bounded scan behavior shared by supported modes.
- `fm_dashboard`, `am_dashboard`, `p25_dashboard`, `adsb_dashboard`,
  `lora_dashboard`, and `pocsag_dashboard` own presentation for their modes.
- Protocol/DSP cores hold host-testable decode logic where already separated.
- `main.cpp` still adapts IQ callbacks, mode policy, audio, tune changes, and
  snapshots into those modules.

Receiver profiles are selected inside the single driver API. Blog V4 is the
tested baseline; Blog V3/V3C and Nooelec profiles exist with experimental
evidence boundaries. V4L and arbitrary RTL2832 receivers are not accepted by
inference.

## Wi-Fi, catalog, and LAN console

ESP-Hosted starts on demand from Settings or deferred saved-profile connection.
The production path intentionally pauses an active radio session around Wi-Fi
scan/connect/power changes and signed catalog I/O, then attempts to restore it.
Documentation must not describe these operations as concurrent uninterrupted
reception.

The optional LAN console starts an ESP-IDF HTTP server on port 80 and advertises
`orcsdr.local`. It serves telemetry, spectrum and audio plus POST
`/api/action` commands for tune, volume/mute, span/step, and dashboard opening.
The server has no TLS or application authentication; it is a trusted-LAN
feature, not a public-network control plane.

The catalog client downloads signed manifests and hash-verifies staged files
before atomic activation. Catalog ownership is bounded: user P25 configuration
remains user-owned, and publication of one catalog does not imply every planned
pack exists.

## Build and verification boundary

GitHub Actions currently runs P25 core, radio-scan core, user-guide, and
Documentation Truth workflows. Native Tab5 firmware is not currently compiled
in CI. Host tests and documentation checks do not prove a physical Tab5,
receiver, antenna, RF signal, display, touch path, or release package.

The Documentation Truth workflow is deterministic and does not invoke an AI
model or consume AI/API credits. It checks dependency/document coherence,
architecture measurement drift, workflow claims, local links, selected file
references, source/document screen IDs, resolved stale claims, and history or
prompt hygiene. It does **not** prove hardware functionality, RF reception,
decoder correctness, antenna suitability, physical display/touch behavior,
feature completeness, software authorship, or whether code was AI-generated.
