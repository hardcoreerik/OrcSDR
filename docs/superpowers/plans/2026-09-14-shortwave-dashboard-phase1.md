# Shortwave Dashboard Phase 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver a dedicated, physically usable Shortwave LIVE dashboard on Tab5 without duplicating the receiver stack or growing `main.cpp` with feature logic.

**Architecture:** Add a first-class shortwave radio band that reuses existing AM DSP, audio, spectrum, recording, and device services. Keep band/data logic, shared tuning-control presentation, and all drawing/touch behavior in focused modules; `main.cpp` remains an adapter.

**Tech Stack:** C++17, ESP-IDF 5.5.4, M5GFX/M5Unified, esp-rtl-sdr 0.8.0-rc2, existing embedded self-check and PowerShell regression harness.

**Spec:** `docs/superpowers/specs/2026-09-14-shortwave-dashboard-design.md`

## Global Constraints

- Do not implement feature logic in `main.cpp`.
- Do not duplicate AM receiver, DSP, audio, spectrum, recording, NVS, or SD code.
- Show only working AM demodulation; do not expose SSB, SAM, CW, or DRM.
- Never dispatch tuner-gain actions during V3c Direct Q operation below 24 MHz.
- Use UTC-compatible and international data contracts.
- Do not claim station identity from frequency or RF energy.
- No new dependency.
- Flash only after host/self-check and native build pass; flashing is authorized.

---

### Task 1: Shortwave model and contracts

**Files:**
- Create: `apps/orcsdr-tab5/ui/shortwave_model.hpp`
- Create: `apps/orcsdr-tab5/ui/shortwave_model.cpp`
- Modify: `apps/orcsdr-tab5/main/CMakeLists.txt`
- Modify: `apps/orcsdr-tab5/ui/main.cpp` (self-check registration only)

- [x] Declare band lookup, tuning steps, filter presets, route/tuning state, and fixed-size station/memory/log structures.
- [x] Add a self-check call before implementation and run the native build to observe the expected missing/failed implementation.
- [x] Implement the ITU-derived band table and validation rules.
- [x] Verify edge, gap, unknown-station, optional-field, and step/filter checks pass.
- [x] Commit the model slice.

### Task 2: Shared tuning-control presentation

**Files:**
- Create: `apps/orcsdr-tab5/ui/receiver_tuning_controls.hpp`
- Create: `apps/orcsdr-tab5/ui/receiver_tuning_controls.cpp`
- Modify: `apps/orcsdr-tab5/main/CMakeLists.txt`
- Modify: `apps/orcsdr-tab5/ui/main.cpp` (self-check registration only)

- [x] Write failing self-check cases for V3c Direct Q and V4 HF-upconverter control availability.
- [x] Implement labels and actions for RF GAIN, TUNER AGC, RTL AGC, AUDIO BOOST, VOLUME, and OFFSET TUNING.
- [x] Keep unsupported controls visible only when explanation is useful; never emit their actions.
- [x] Verify the focused self-check passes.
- [x] Commit the shared-control slice.

### Task 3: Dedicated Shortwave LIVE dashboard

**Files:**
- Create: `apps/orcsdr-tab5/ui/shortwave_dashboard.hpp`
- Create: `apps/orcsdr-tab5/ui/shortwave_dashboard.cpp`
- Modify: `apps/orcsdr-tab5/main/CMakeLists.txt`
- Modify: `apps/orcsdr-tab5/ui/screen_controller.hpp`
- Modify: `apps/orcsdr-tab5/ui/screen_controller.cpp`
- Modify: `apps/orcsdr-tab5/ui/main.cpp` (adapter and lifecycle wiring only)

- [x] Define the snapshot/action API and failing touch/self-check expectations.
- [x] Draw the LIVE screen with frequency, band, AM, filter, route, signal, spectrum, tuning step, and shared tuning panel.
- [x] Add 100 Hz, 500 Hz, 1 kHz, and 5 kHz stepping plus recognized-band navigation.
- [x] Show the five product tabs while making non-LIVE tabs explicitly unavailable in Phase 1.
- [x] Add the first-class shortwave band to shared receiver routing and reuse AM demodulation/audio AGC.
- [x] Forward spectrum and touch data through the adapter and verify the self-check.
- [ ] Commit the LIVE dashboard slice.

### Task 4: Regression, build, and hardware acceptance

**Files:**
- Modify: `apps/orcsdr-tab5/tools/run-tab5-ui-regression.ps1` only if a focused Shortwave serial check is required.

- [x] Run `git diff --check`.
- [x] Run `& .\apps\orcsdr-tab5\tools\run-tab5-ui-regression.ps1 -SelfCheck`.
- [x] Run `& .\apps\orcsdr-tab5\tools\build-tab5-idf.ps1`.
- [x] Record exact artifact hashes and app size.
- [ ] Flash the clean artifact to the authorized connected-dongle Tab5.
- [ ] Verify serial device identity, Shortwave entry, exact tuning, route, IQ continuity, and no invalid gain commands.
- [ ] Ask the user to judge physical touch/UI behavior and audio separately; treat the dipole as unsuitable/unknown for reception acceptance.
- [ ] Commit only any evidence-driven fixes after a failing check reproduces them.

## Deferred work

- AM/FM migration to the shared tuning panel follows after Shortwave proves the control model.
- MEMORY/LOGBOOK persistence and CSV export are Phase 2.
- ON AIR, offline schedule packs, and station matching are Phase 3.
- RF HUNT and schedule correlation are Phase 4.
- ADIF SWL, propagation, maps, and additional demodulation modes are Phase 5.
