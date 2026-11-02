# HFDL dashboard implementation handoff — 2026-11-02

Branch: chatgpt/hfdl-dashboard-decoder (only). Origin/main and LilyGO branches remain untouched.

## Completed source changes

- Existing HFDL files extended: hfdl_dashboard_model.hpp/.cpp, hfdl_dashboard_ui.hpp/.cpp, hfdl_dashboard.hpp/.cpp, hfdl_decoder.hpp/.cpp.
- Added layout: apps/orcsdr-tab5/ui/hfdl_dashboard_layout.hpp. Native layout generation only for 1280x720, 720x720, 800x400 and 568x1232. No "scale a Tab5 bitmap" workaround.
- CMake now includes HFDL Model/Controller/Renderer/frontend. Registry includes an aviation HFDL (LAB) descriptor marked unavailable until real receiver integration.
- Changed a dangerous fake 32-second periodic SPDU into *no message event*; Decoder frontend translates +1440 Hz, filters (127 taps), decimates to 12 kS/s, measures channel power, exposes a 64-complex-sample sink for an eventual real 1800 Baud demodulator. No synthetic HFDL message can reach the UI from the frontend.
- Fixed queue overflow accounting, full 8-usable-entry SPSC semantics and UI data race (producer updates atomic state; consumer owns dirty/snapshot/ring); enabled newest-first bounded message history, pagination, detail lookup and bounded text length.
- Added true M5GFX-compatible list/detail rendering. Drawing uses verified Snapshot records only, 3 view components (Model, Controller, renderer), per-display hit targets, shared Tab5 header/status reservation, native small-panel title and badge, overlay guard. No full-frame buffer introduced.

## Explicit display test contract

Panel | Expected logical viewport | Message rows | Header
--- | --- | --- | ---
M5Stack Tab5 | 1280x720 landscape | 7 | Existing audio_header brand/status
Waveshare square | 720x720 | 6 | Native compact badge/title
Hosyond | 800x400 | 3 | Native condensed badge/title
LILYGO T-Display P4 AMOLED | 568x1232 portrait | 8 | Dedicated portrait badge/title

The host tests verify all generated rectangles stay inside their intended viewport, visible row rectangles do not overlap, touch actions reach their matching controls, and radio-data updates never issue a full-screen clear. **Not a device validation:** on P4 ScaledGfx the reported width/height may be a *logical design grid*, not the hardware panel size. The board must configure native 720x720 / 800x400 / 568x1232 logical drawing and map touch to those dimensions before HFDL enters. Do not blindly use a 1280x720 logical grid for portrait output.

## Verified GitHub host test evidence

Workflow: https://github.com/hardcoreerik/OrcSDR/actions/workflows/hfdl-dashboard-tests.yml
Initial green after native renderer: https://github.com/hardcoreerik/OrcSDR/actions/runs/41539982059
Green after channel sink: https://github.com/hardcoreerik/OrcSDR/actions/runs/41540205511
Green after registry test: https://github.com/hardcoreerik/OrcSDR/actions/runs/41540454634

Run from the repository root on Linux:
    bash tools/test-hfdl-dashboard.sh
Outputs in the same execution folder: build/hfdl-dashboard/test.log, hfdl-dashboard-tests, hfdl-render-tests, hfdl-registry-tests. A host pass does NOT establish full ESP-IDF firmware compilation or physical-device no-flicker.

## Application integration still pending

- Firmware main.cpp and device-specific ScaledDisplay configuration were not rewritten, as this branch's huge main.cpp cannot be fetched reliably through this connector and parallel P4 display branches are in progress. The menu descriptor remains unavailable, rather than a clickable unimplemented action.
- No IQ receiver adapter or verified HFDL decoder backend is linked to the frontend; the shared radio pipeline must supply audio/IQ after lawful receive-only tuning and controlled channelization, on a worker outside the UI task.
- Receiver workers must invoke Model::post_message only with independently CRC-verified frames and correctly assigned source metadata. A valid sink block is **not** a decoded HFDL message.
- No real hardware was flashed, touched, or instrumented.

## Next work gates

1. Bring branch into a controlled integration with codex/p4-display-integration + claude/lilygo-tdisplay-p4-amoled after each concurrent branch has an agreed merge base. Keep this HFDL branch unmerged meanwhile.
2. Implement A1/A2 acquisition, M1 signaling, 1800 Baud timing and PSK soft demapper / HF equalization from ICAO Annex 10 Ch 11 and REA-Results HFDL notes. Match xng/dumphfdl's CRC-valid squitter on a pinned real IQ recording, saving hash, command, time, decoder output and frame-exact evidence.
3. Implement FEC and SPDU/MPDU/LPDU/HFNPDU parsing, preserving distinction between ground station reports and airborne positions. Share application-level ACARS/ADS-C decoding with OrcSDR once validated.
4. Complete application lifecycle (one-time PSRAM state allocation, decoder worker/driver handoff, dashboard navigation, focus, overlay ownership) in a checkout where the current main.cpp and P4 profiles are fully accessible. Keep source not-ready semantics.
5. Compile the full ESP-IDF Tab5 firmware and variants. Capture actual M5GFX screenshots and measure 1280x720, 720x720, 800x400, 568x1232: touch/rotated coordinates, font clipping, flicker, PSRAM, USB/IQ drop counters, CPU/render time.
6. Repeat ITU-R F.1487 multipath and P.372 interference tests against the same IQ input using dumphfdl and xng as reference.

## Limits / safety

No paid standard was purchased. No transmissions or proprietary firmware analysis. No success or measured sensitivity claims. The present frontend is an honest DSP preprocessor, not a frame decoder, and the dashboard displays no invented aircraft, live packet or network availability.
