# OrcSDR SSTV Auto (VIS + Sync) — implementation handoff

**Development branch:** `codex/sstv-native-dashboard`  
**Primary architecture:** M5Stack Tab5 / ESP32-P4, existing RTL-SDR 48 kHz PCM producer -> 12 kHz SSTV decoder UI service.  
**Status:** This document describes an *experimental* AUTO decoder implementation and required Claude follow-up. Not a merged firmware release.

## Update 2026-10-12 (second developer pass) — status

The work this handoff asked for is implemented on `claude/sstv-auto-dashboard` (child of this branch) and described in `README.md`. What changed relative to the list below:

- **A. Dashboard integration — done.** Auto / VIS only / Manual strategies, honest identification wording, gallery chips, Signal diagnostics, persisted settings. Manual remains a labelled **VIS filter**; decoding without VIS is not implemented. Logic lives in `sstv_app`, `sstv_settings`, `sstv_status`; `main.cpp` keeps only hook adapters.
- **B. Background SD capture — done on the host.** `sstv_archive` (queue, worker, `.part` + rename, schema v2, recovery, retries) with reference-counted frames; `save_bmp()` is removed. Fault-injection tests cover missing, full, pulled and slow cards.
- **C. Robustness — fixed:** 64-bit sample clock (the 99.4 h rollover was real: no line committed after the wrap), dropped-audio gap compensation, silence handling, per-session counters, end-of-stream flush for offline files. Not changed: the 48→12 kHz decimator and the Robot 36 / Robot 72 pulse alias (documented).
- **D. Hardware budget — measured on the host only.** Counters exist (service time, CPU share, ring high-water, PSRAM frames, SD write latency) and are shown on the Signal page; **no Tab5 measurements have been taken**. Nothing has been flashed.

Open items: on-device validation, P4 timing, long soak, cabled RF tests (never started automatically), colour.

## What is already implemented and committed

1. `ui/sstv_auto_detect.hpp/.cpp` — standalone, fixed-size eight-pulse 1200 Hz sync history; classifies 14 currently implemented SSTV formats using reference line-period and sync-duration measurements; requires >=6 coherent pulses, >=75 heuristic score, >=12 best-vs-runner-up score margin. No dynamic allocation, full-frame buffering, or parallel decoders. Relative classifier confidence is **not** a calibrated probability.
2. `ui/sstv_receiver.cpp` — default Auto strategy `vis_sync`; fall back on pulse classifier only while idle and not amid an active VIS preamble. Start from the NEXT full line after lock, accounting for Scottie's mid-line sync. Correctly mark unknown frame origin and all sync-only captures as partial.
3. VIS demodulator now gathers 20 ms of symbol votes per 30 ms VIS bit, checks parity/stop bit, and continues listening for a *new* VIS while receiving. Valid new VIS archives previous image as partial and begins new format, without RTL-SDR restart.
4. Live decoder attempts to limit missing sync holdover to four predicted rows. On loss, it archives the partial image and returns to detection. New metadata: `Identification`, `EndReason`, `start_unknown`, `identification_score`, `partial_frames`, `auto_sync_locks`.
5. Replaced absolute `float` zero-cross timestamps (which lost sub-sample resolution in 23 minutes) with `uint32_t` sample index + fractional crossing. **The separate `double g_line_origin` raster clock still needs a 32-bit wrap check** for >~99 hours continuous sessions.
6. `ui/sstv_receiver.hpp` exports `set_auto_strategy(AutoStrategy)` and `auto_strategy()`. `vis_only` opt-out is testable; `vis_sync` is the firmware default.
7. `tests/sstv_auto_detect_test.cpp` covers all 14 timing signatures, irregular noise, timestamp rollover. `tests/sstv_receiver_smoke.cpp` covers legacy Robot36, PD120, Scottie S1, 1 ms luma stripe resolution, missing VIS partial, VIS-only disabled fallback, new PD120-to-Robot36 VIS session, automatic archiving. `.github/workflows/sstv-receiver-tests.yml` builds/runs tests and publishes a replay executable and sources.
8. `main/CMakeLists.txt` and `tools/sstv_rea_workbench.py` include the new classifier module. The host WAV replay tool still works on the user's received Robot36 48kHz PCM16 mono WAV.

## Evidence observed in our lab

- Real Pluto RF capture `received-filtered-aligned.wav` (36.91s): VIS 8, Robot36, 239 image rows *without trailing samples*, 240 sync pulses, zero missed syncs, zero ring overruns. Offline adding ~5ms silence closes final row; do not silently pad truncated real recordings in production.
- Same recording with first 2.0s removed and 2s of silence appended: automatically classified Robot36 using sync alone (VIS=0), ~229 partial image rows, 226 sync locks, 4 missed sync lines, no overruns. The image's actual top row remains unknown; no full-frame claim.
- Latest comprehensive host run: https://github.com/hardcoreerik/OrcSDR/actions/runs/38336567762 (pass). Earlier failed workflow runs remain visible historically; judge HEAD-specific CI.
- A new full ESP-IDF firmware compile was queued for the integration. **Check current run completion** before claiming it passed. This work has not been flashed/tested on the physical Tab5.

## Critical limitations and expected Claude implementation work

### A. Real dashboard integration

The existing native dashboard `ui/sstv_dashboard.cpp` has five screens: Receive, Images, Signal, Frequencies, Setup. Do not replace or redesign them from scratch. Extend them with a coherent Auto mode control:
- User strategies: **Auto (VIS + Sync)** [recommended], **VIS Only**, **Manual** [current `set_mode` already provides a VIS match filter; manual decoding *without VIS* is not yet implemented and must not be claimed].
- Show detection method (VIS-confirmed vs sync-likely vs unknown), actual code only if known, heuristic confidence / pulse count where applicable, predicted vs observed sync and partial-origin note.
- Persist Auto strategy and manual mode using the existing `preferences` NVS mechanism in `ui/main.cpp`; restore before `sstv::start`. Never reset the receiver or discard progress solely for a UI redraw.
- Display partial images distinctly; do not present sync-only captures as original complete frames.

### B. Background SD capture

Currently `ui/main.cpp` invokes `save_bmp()` synchronously on the M5GFX UI task. This is UNSAFE for unattended SDR reception when microSD stalls. Create a low-priority storage worker/queue with bounded jobs, stable framebuffer ownership, no double-free, configurable auto-save, and predictable failure if SD unavailable/full.
- Store **all** finished and interrupted captures (if user enabled) with mode, reason, identification source, confidence, partial/unknown-start metadata and per-session missed sync counts. Extend `/sd/orcsdr/sstv/index.csv` compatibly or version its schema. Use `.part` then rename and append-only index. Recover orphan BMPs after power loss. Never block USB/PCM producer or sample service.
- Gallery is currently four RGB565 PSRAM slots and has no permanent indexed SD browser. Ensure no free of image still pending storage.
- Content hashes can detect duplicate IMAGE CONTENT; do not equate identical names or omit distinct reception event logs.

### C. Robustness / DSP correctness

- Review Auto `sync_edge` timing decisions and lock alignment for all 14 modes, especially Martin 4.862ms sync, Scottie mid-line sync, PD paired luminance scans.
- A sync-only mid-frame join can infer mode and phase but **cannot know absolute original row index**; maintain cropped/partial semantics and no fabricated top content. Consider a rolling small audio buffer if restoring one or two lines after lock materially improves quality, never entire PCM frame in RAM.
- Four consecutive missed line syncs currently terminate and archive; this is an intentionally conservative provisional threshold, not universally optimal. Test fading, noisy audio, clock drift and false locks; separate real lock loss from silence/noise and support cautious relock.
- VIS vote validation rejects ambiguity; compare against noisy captures. A new VIS can arrive while old lines are running; make state transitions safe and once-only.
- Keep source-documented F4HWN normalized autocorrelation luminance (alpha 0.6, 513-entry LUT) and verified Robot36 sharpness improvements. Do not regress it by moving back to zero-cross luminance.
- End-of-WAV original 239/240 behavior: implement explicit **offline decoder flush/EOF** semantics in the test harness, not fabricated received silence or an arbitrary full-frame claim. Never assert a full frame from incomplete RF.
- Zero-crossing uses wrap-safe uint32 ticks; raster uses double origin which will fail on sample-counter rollover around 99 hours. Fix with bounded/modular timing before unattended qualification.
- Existing 48->12k decimator is a four-sample moving average; investigate aliasing only if measurements justify a change.
- Current `set_mode` manual setting is VIS match filter, not manual forced start. Document the distinction.

### D. Hardware execution budget and tests

- Keep producer ring = 4096 PCM16 samples (8KB), luma scratch = 2x800 B (1.6 KB), autocorrelation LUT = 513x2 B (~1 KB). Classifier has only 8 pulse history elements.
- One RGB565 framebuffer at a time per session; PD120 640x496 ~620 KiB and PD290 800x616 ~963 KiB of PSRAM, plus gallery/other SDR and display resources.
- Zero allocations/blocking I/O in PCM callback and SSTV sample loop; bounded per-service CPU and bounded image queue.
- Run `g++` host regressions and CI, ESP-IDF 5.5.4 firmware compile, and on-device USB/NFM/SD/UI tests. Measure P4 CPU cycles, memory usage, max PCM queue fill, overruns and long-run image count. Do not substitute host benchmark for Tab5.
- Add fixture scenarios: Robot36 -> PD120 -> Scottie S1 without interaction; VIS corrupted; truly missing VIS; clipped header; arbitrary noise; interrupted and adjacent transmissions; >99h simulated clock wrap; 2x or 0.5x missing pulse alias; no-SD/slow-SD/SD removed; retune; user abort; four-gallery eviction with storage worker; repeated frames hash.
- Save timestamped host test logs to **the directory where invoked**, and a deterministic summary with fixture SHA256, supported modes, rows, identification source, complete/partial and input settings.

## Useful local test commands

```sh
git switch codex/sstv-native-dashboard
g++ -std=c++17 -O2 -Wall -Wextra -Iapps/orcsdr-tab5/ui \
  apps/orcsdr-tab5/tests/sstv_receiver_smoke.cpp \
  apps/orcsdr-tab5/ui/sstv_receiver.cpp \
  apps/orcsdr-tab5/ui/sstv_modes.cpp \
  apps/orcsdr-tab5/ui/sstv_auto_detect.cpp \
  apps/orcsdr-tab5/ui/sstv_auto_detect.cpp -o sstv-smoke
./sstv-smoke > sstv-host-test.log 2>&1

g++ -std=c++17 -O2 -Wall -Wextra -Iapps/orcsdr-tab5/ui \
  apps/orcsdr-tab5/tests/sstv_wav_replay.cpp \
  apps/orcsdr-tab5/ui/sstv_receiver.cpp \
  apps/orcsdr-tab5/ui/sstv_modes.cpp \
  apps/orcsdr-tab5/ui/sstv_auto_detect.cpp \
  apps/orcsdr-tab5/ui/sstv_auto_detect.cpp -o sstv-wav-replay
./sstv-wav-replay /path/to/received-filtered-aligned.wav 8 239 \
  > sstv-pluto-replay.log 2>&1
```

Use the repository's qualified `tools/build-tab5-idf.ps1` or SSTV-specific CI for the firmware build. Never use PlatformIO for the Tab5 build.

## Required completion report

For Claude: review every source change, fix actual CI failures before feature work, preserve the current native dashboard, finish settings integration and background SD capture, prove multi-mode transition and honest partial semantics with host fixtures, test hardware when available, then summarize commits, changed paths, measured outcomes and remaining limitations. Do not push to `main` or create a release without explicit approval.
