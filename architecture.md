# OrcSDR current architecture

This describes the current source architecture on 2026-10-07. Historical plans
and validation reports explain how the design arrived here but are not current
architecture contracts.

The accepted Stage-1 production DSP path (developed on `claude/dsp-multirate`,
formerly draft PR #114) is part of `main` as of v0.3.0-beta.1; the Stage-2 multirate
lab is not production architecture. Historical evidence is in the dated
[DSP closeout](docs/validation/dsp-stage1-stage2-closeout-2026-09-27.md).

## Platform and dependency boundary

OrcSDR's production Tab5 firmware is a native ESP-IDF 5.5.4 application for
ESP32-P4. The onboard ESP32-C6 runs matching ESP-Hosted 3.0.6 firmware and uses
4-bit SDIO at the qualified 10 MHz clock. PlatformIO configurations are
historical and unsupported.

The portable receiver boundary is the external `esp-rtl-sdr` component,
published release `v0.9.3` (it reports `0.9.3`), pinned immutably at
`31a159df4f006a837d5041029bc6d1bc63520234` by
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

main.cpp measurement (Git-normalized): 946,879 bytes (~924.7 KiB), 20,583 lines.

The measurement uses LF-normalized repository bytes so it is stable across
Windows and Linux checkouts. The intended modular endpoint—roughly 500 lines of
application wiring, setup, loop, and task creation—has not been reached. This
documentation task does not refactor it.

## Display and dashboard ownership

`screen_controller` is the single framebuffer owner. A transition grants one
screen permission to draw while radio/decoder work continues independently.
`navigation_service` owns Home/Settings handoff mechanics; feature dashboards
render snapshots rather than owning receiver state.

ScreenController IDs: `none`, `home`, `fm`, `p25`, `adsb`, `lora`, `radio`, `visualizer`, `rf_lab`, `wifi_analysis`, `pocsag`, `settings`, `am`, `shortwave`, `documentation`, `cb`, `airband`, `weather`.

Dashboard IDs: `home`, `fm`, `p25`, `adsb`, `shortwave`, `weather`, `cb`, `lora`, `airband`, `marine`, `satellite`, `utilities`, `settings`, `rf_lab`, `wifi_analysis`, `pocsag`, `am`.

The current screen modules include Home, FM, AM, Weather, P25, ADS-B, LoRa,
POCSAG, RF Lab, RF Visualizer, Wi-Fi analysis, Settings, documentation capture,
and the shared Radio/Scope/Capture surface. Weather owns a dedicated five-tab
presentation/runtime/report layer but reuses the existing WX/NFM receive path;
opening the Weather screen alone does not tune or take receiver ownership.
Airband has its own dashboard (`airband_*` modules: scanner, catalog, runtime,
dashboard; see `docs/airband/README.md`) on `radio::Band::airband`, using the
shared AM demodulator and receiver controls. Dashboard catalog entries for
Shortwave, Marine, and Satellite route into shared/experimental receiver
surfaces; catalog labels do not imply dedicated decoders or complete
demodulation modes.

## Receiver and DSP ownership

- `radio_session` serializes receiver ownership and generation changes. Explicit Weather RF work uses `Owner::weather`; the legacy `Band::wx` owner mapping remains `Owner::radio` for older callers.
- `scan_engine` supplies bounded scan behavior shared by supported modes, including the seven-channel Weather Radio scan. A completed Weather scan caches its strongest relative-dBFS observation and stops; continuous listening is a separate explicit action.
- `fm_dashboard`, `am_dashboard`, `p25_dashboard`, `adsb_dashboard`,
  `lora_dashboard`, and `pocsag_dashboard` own presentation for their modes.
- Protocol/DSP cores hold host-testable decode logic where already separated.
- `main.cpp` still adapts IQ callbacks, mode policy, audio, tune changes, and
  snapshots into those modules.
- `rtl_dsp_task` alone owns stateful demodulator/RDS state.
  Other tasks request demod, RDS, SSB-BFO or full resets; the DSP task applies
  them in deterministic order at IQ-block boundaries. Block-local hot state,
  fused clipping/level work, batched RDS, measured FM/SSB `noinline` and the
  overload yield remain in the normal path.
- `rtl_default_sample_rate()` selects 2.40 MS/s device acquisition for FM,
  NFM/Weather, AM, CB, Shortwave and Browse; other mode defaults differ.
  The current WFM demodulator reduces that to 240 kS/s MPX and 48 kHz audio.
  Device rate, spectrum/analysis rate and audio-demod rate are distinct.
  Custom-rate RF Lab acquisition does not imply audio at that rate.
- D/D2/D3 and their live exclusive-routing commands are compiled only with
  `ORCSDR_DSP_LAB=1`; the normal builder explicitly passes `0`, as it does
  for the independent Stage-1 A/B harness. No experimental frontend replaces
  the existing production demodulator.

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
