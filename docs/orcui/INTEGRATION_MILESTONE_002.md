# OrcUI adaptive-display integration contract — Milestone 002

**Status:** research architecture contract. No physical ESP32-P4 UI acceptance is asserted here.

## Objective

Implement a *single shared* OrcSDR Home UI that adapts to 1280x720, 720x720, portrait, round, and ultrawide environments. The same Home state, commands, widgets and adaptive placement rules must be used across displays. A Waveshare ESP32-P4-WIFI6-Touch-LCD-4B (720x720) is the first hardware integration and performance test; the current M5GFX Tab5 UI remains the untampered production baseline.

## Real repository starting points

- Development branch: `codex/orcui-adaptive-display-poc`; `experiments/orcui/layout.cpp`, `raster.cpp`, `home_lvgl.cpp`, `include/orcui/*`, and `tests/layout_test.cpp`.
- Hardware code source: `codex/waveshare-4b-fm-dashboard`; `apps/orcsdr-waveshare-4b/` with Waveshare BSP 3.0.1, LVGL 9.3.0, esp_lvgl_adapter 0.6.3, and pinned esp-rtl-sdr v0.9.3 commit 31a159d.
- The Waveshare source contains a 720-specific `home_v3_drawer_layout.hpp` and `fm_4b_layout.hpp`. The earlier `components/orcsdr_ui_layout/include/orcsdr_ui_layout.hpp` is another independent layout experiment.
- Host-only `experiments/orcui` tests do not prove an LVGL native build; Waveshare FM native build evidence does not prove the shared OrcUI Home works on hardware.
- The owner's physical 4B has previously reported ESP32-P4 silicon v1.3, 32 MiB PSRAM @200 MHz, 32 MiB flash, GT911 touch. These are *historical recorded observations*; preflash probe the current connection and do not assume a serial port.
- The Waveshare board's OTG USB-C power source role is nonstandard. Real SDR reception previously required a verified independently powered HS host-data fixture. No 5 V injection, CC modification or VBUS backfeed.

## Canonical architectural contracts

1. **One canonical adaptive geometry policy.** For this integration, use `experiments/orcui/include/orcui/layout.hpp` and `experiments/orcui/layout.cpp` as the Home layout authority. Do not create another 720-coordinate table. The old Waveshare layouts may remain as a reversible legacy baseline, but the new OrcUI view must not use them.
2. **One real Home view implementation.** `orcui::HomeLvgl` is the shared LVGL presentation. It receives data/actions from a small board adapter; it must not own radio tuning, USB Host, P4 power rails, decoder state, or BSP initialization.
3. **One real event model.** Enrich `HomeData`/`HomeCommand` as necessary to express existing live Waveshare Home behavior (badge/drawer, tune +/- , step, palette, FM IQ preview route) through *capability-gated* commands. Don't hardwire a second Waveshare version of each button. Missing features must be disabled/marked unavailable; no fabricated audio/RDS/station state.
4. **Single display owner.** Board BSP / `esp_lvgl_adapter` initializes panel/touch, and the LVGL task owns widget creation/updates. All UI API calls happen under the vendor BSP display lock where required. SDR callback/DSP tasks transfer bounded snapshots, never render or wait for the LVGL UI.
5. **RF graphics are reusable data-driven views.** `paint_spectrum` / `push_waterfall` take width, height, stride and FFT source independently of panel geometry. Store large RGB565 buffers in PSRAM using an explicit and tested allocation policy. Verify cache / lifetime / DMA synchronization for the chosen adapter. No buffer reallocation when merely opening the drawer.
6. **One board adapter, not one dashboard.** Put Waveshare-specific startup, touch, calibration, refresh scheduling and `home::Command` → SDR request dispatch behind a narrow adapter outside the shared OrcUI Home implementation. Preserve the proven USB owner and rev1.x BSP.
7. **Feature parity measured, not assumed.** Baseline the existing 4B Home v3 and its honest IQ-only FM preview, then test the adaptive Home on the same board. Preserve a build-time or runtime fallback to the legacy view until the adaptive version passes.

## Integration checks

- Host: CMake/CTest OrcUI layout+raster tests on 1280x720, 720x720, round 480/360, portrait, and 2560x720, including bounds, overlap, accessible taps, drawer backdrop interception and state/action continuity.
- Native: independent ESP-IDF 5.5.4 Waveshare P4 rev1.x build with pinned BSP/adapter/LVGL/driver, recorded firmware SHA, verified bootloader/partition/flash and 200 MHz PSRAM settings; no production Tab5 dependency bumps.
- Physical UI: badge toggles drawer and outside tap dismisses *without click-through*, real Orc badge, correct color/labels, actual center frequency, waterfall palette, actual vs unavailable telemetry, touch +/- and STEP, optional FM preview navigation and return, and reboot/resume.
- Radio: with known-good electrically safe powered HS fixture, capture `HOME_4B RF START` + >=5 minutes of IQ metrics; report effective sps, drops, rendering p50/p95, minimum internal DMA heap, PSRAM usage, uptime/crashes, and whether any decoder/audio path was *not* tested.
- Explain screenshot/photo evidence vs simulated UI and legacy vs adaptive builds. No assertion of WFM audio or RDS parity from IQ-only tests.
- Log commands and output in the current invocation directory; report to https://github.com/hardcoreerik/OrcSDR/issues/195 with branch SHAs and reproducible observations.

## Safety and branching

- Do not merge or overwrite `main`, `codex/orcui-adaptive-display-poc`, or `codex/waveshare-4b-fm-dashboard` during local implementation. Work in isolated worktrees and a `local/orcui-4b-*` branch based on the OrcUI development branch, importing exact, recorded Waveshare files as necessary.
- User has explicitly granted permission to flash **only their Waveshare P4 4B** for the OrcUI test program. This authorizes non-destructive application flashing after the local agent positively matches the board and port, revision, flash layout, firmware hashes and supported power arrangement. It does not authorize chip erase, NVS erase, C6 flashing, partition migrations or flashing other boards.
- On any uncertain target identity, unsafe power routing, destructive partition requirement, build failure, or unresolved DMA ownership conflict: STOP and report BLOCKED.
- Use the current `handoff.md` as the execution order; this document defines the acceptance contract, not a separate automated loop.
