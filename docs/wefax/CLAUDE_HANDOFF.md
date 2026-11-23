# WEFAX native M5GFX dashboard and schedule catalog — Claude handoff

Date: 2026-10-28. Branch: chatgpt/wefax-decoder-development. Existing draft PR: #218.

## Status: code committed, physical integration pending

Already implemented on this branch:
- apps/orcsdr-tab5/ui/wefax_schedule.hpp/.cpp: allocation-free, host-testable NOAA station catalog and UTC schedule model, with correct assigned-to-USB 1.9 kHz offset. Includes NMC Point Reyes (five frequencies), NMF Boston (four frequencies) and only 15 CURATED daily broadcast starts (NOT the complete NOAA schedule). Never infer signal presence from the calendar.
- apps/orcsdr-tab5/ui/wefax_dashboard.hpp/.cpp: real native M5GFX presentation code using M5.Display. Two touch-selectable station tabs, frequency +/- buttons, assigned and USB dial readouts, verified-clock-only upcoming program list, actionable Tune USB request (not direct tuner access), true decoder-disconnected and clock-unverified states. Geometry adapts to 1280x720 landscape and 720x720 square displays. Header hook available for shared controls.
- apps/orcsdr-tab5/ui/wefax_core.hpp/.cpp: an earlier manually-framed host-tested decoder. Not yet subscribed to live RF/audio, and not automatic APT/phasing.
- tools/wefax_schedule_test.cpp, tools/wefax_dashboard_host_test.cpp, tools/wefax_fakes/M5Unified.h, tools/wefax_schedule_host_test.sh: offline C++ regression and fake-M5GFX redraw/touch tests.
- tools/wefax_pluto_lab.py, tools/wefax_wav_replay.cpp, tools/wefax_pluto_host_tests.sh and docs/wefax/PLUTO_BENCH_README.md: previous conducted-RF test harness, not yet validated over actual WEFAX wire.
- apps/orcsdr-tab5/main/CMakeLists.txt: wefax_core.cpp, wefax_schedule.cpp and wefax_dashboard.cpp are now registered to compile in the firmware, but no ESP-IDF build has been run yet.

NOT DONE: main.cpp navigation routing, separate wefax_app adapter, dashboard_registry and screen_controller IDs, authenticated radio-session tuning, production audio tap, live image/gallery, automatic phasing/start/stop, SD archive, station data updates, 720 square hardware screen acceptance, real RF or HF testing. Do NOT claim this screen is reachable from Home or that WEFAX live reception is ready.

## Exactly how to pull and test

Preserve existing OrcSDR checkout and use the established WEFAX branch; do not create a second branch or merge into main:

    cd F:\Ai\OrcSDR
    git fetch origin
    git switch chatgpt/wefax-decoder-development

Alternatively create a detached separate worktree if other work must be preserved.

Run from Bash/WSL with g++:

    bash tools/wefax_schedule_host_test.sh
    bash tools/wefax_host_test.sh
    bash tools/wefax_pluto_host_tests.sh

The schedule runner writes wefax_schedule_host_test.log INTO THE DIRECTORY IT IS INVOKED FROM, and should output both WEFAX_SCHEDULE_PASS and WEFAX_DASHBOARD_PASS. The existing core runner writes tools/wefax_host_test.log; Pluto runner is an offline test and must not start RF by itself. Keep all logs beside invocation and record the exact checked-out commit SHA.

Read apps/orcsdr-tab5/ui/AGENTS.md, docs/UI_RENDERING_STANDARD.md, docs/TAB5_BUILD_POLICY.md, architecture.md and docs/sstv/README.md before any actual UI/radio integration.

## Integration implementation to finish

1. Complete the normal supported ESP-IDF 5.5.4 Tab5 build first, without altering C6 firmware, NVS, SD, or map partitions. CMake source inclusion alone is NOT a build pass; record compiler diagnostics and code/memory size.
2. Add screen_controller Id::wefax AFTER maps to preserve existing ordinal values. Extend screens::name.
3. Add dashboards::Id::wefax at the end (before count if applicable) and a native WEFAX descriptor. Update registry count assertions and persistence guards. Do not hijack NOAA WEATHER, VHF MARINE or SSTV dashboards.
4. Create a small wefax_app adapter following sstv_app or acars_app lifecycle, rather than moving receiver, UI, and storage into main.cpp. Route Home/Settings/navigation with screen_controller ownership and use wefax_dashboard::enter/update/touch/leave only on the UI task. Install shared header hook.
5. The UI touch returns ActionKind::tune_usb and the USB *dial* frequency in Hz. Application code must check receiver capability and session ownership, explicitly select USB (do NOT rely on SSTV AUTO's below-10MHz LSB convention), request a safe retune and show true refusal/confirmation. Ex: NMC 8682 kHz assigned -> 8680.1 kHz dial, NMF 6340.5 -> 6338.6 kHz dial. Single tuner: never auto-retune over another active dashboard.
6. Supply verified_utc_epoch ONLY from OrcSDR's validated UTC time service. If unknown supply 0 and retain the visible UTC CLOCK UNVERIFIED state; no invented countdown or local timezone. Update the model on minute boundaries without clearing the whole display.
7. Set decoder_connected ONLY when actual PCM delivery is connected and validated. Supply true received_rows from the decoder; clear on retune/stop. The decoder currently needs manual framing, so leave production live reception disabled until phasing/auto-start support is genuinely built and measured.
8. On exit restore/release the radio session correctly. Keep archive writes and slow operations out of the display task. Add physical touch and pixel-clip checks on Tab5 and 720x720 square boards.

## Required acceptance gates (report separately)

- HOST: catalog, decoder and Pluto offline tests, verified from clean branch and logs.
- FIRMWARE: native ESP-IDF compiler, linker and memory budget (do not substitute host fake-M5GFX pass).
- NAV: Home -> WEFAX -> Home, Settings, repeated enter/exit, screen ownership and shared header touch behavior.
- UI: 1280x720 and 720x720 physical display/touch, changing status and UTC minute without fullscreen clears or flicker; confirm rendered text is visible/legible; measure draw cost and DMA/PSRAM headroom.
- RF TUNER: correct HF USB actual dial, observed demod state and rejected unsupported receiver. Real carrier/signal reception is NOT implied by schedules.
- CONDUCTED PLUTO: only verified low-power TX1 -> 30 dB pad -> cable -> RTL-SDR, no antenna, recalibrate WEFAX level, verify source mute / DMA continuity and actual input level before RF. Historic device was 192.168.1.228; re-check identity and do not reuse as proof. Prior -71.5 dBm SSTV reading does not qualify a new waveform.
- LIVE HF: real broadcast and off-air charts separately, with exact source WAV and pixel-scoring evidence.
- Mark PASS, FAIL, NOT TESTED separately for each. Never aggregate an unrun hardware test into PASS.

## NOAA provenance / limitations

Checked official NOAA pages on 2026-10-28 (pages still carry September 19, 2018 schedule headers):
- Pacific NMC https://ocean.weather.gov/shtml/pacsch.php
- Atlantic NMF https://ocean.weather.gov/shtml/atlsch.php
- Worldwide and other stations https://ocean.weather.gov/fax_schedules.php

Published assigned frequencies are NOT necessarily active at the current moment or best at the user's QTH. Day/night are NOAA channel labels, not an OrcSDR SNR prediction. The offline catalog contains ONLY curated sample products (including daily test-pattern slots). A complete, versioned, checksum-validated schedule data pack and optional updates are future work; do not misrepresent the built-in subset as a worldwide comprehensive schedule.

## Handoff exit condition

Claude should first make the EXISTING M5GFX schedule view accessible and tune correctly on the Tab5, preserving other screens. Test actual M5GFX draw/touch before working on automatic phasing or live image capture. Continue on the SAME WEFAX branch and draft PR #218; no merge to main without explicit approval. Document any change with tested SHA, real logs, and clear hardware limitations.
