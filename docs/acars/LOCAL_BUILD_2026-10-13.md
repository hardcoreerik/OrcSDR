# ACARS local firmware build record - 2026-10-13

Source handoff: 55deedaad8a0d80d3147848361885214184af707, branch claude/acars-dashboard-ui, draft PR #189, base codex/acars-native-decoder.

## Build and fixes

Command: `apps/orcsdr-tab5/tools/build-tab5-idf.ps1` from the checkout root. ESP-IDF 5.5.4; standard defaults and verified cached ESP-Hosted 3.0.6 image embedded. No flash command or device access was performed.

First build: FAIL. Seven `-Werror=format=` errors at original acars_dashboard.cpp lines 149, 153, 156: `%u` expects unsigned int but uint32_t arithmetic is unsigned long on the ESP32 toolchain. Fix: explicit unsigned int casts of bounded MHz, UTC and uptime display values. No warning suppression or test changes.

Rebuild: PASS. M5GFX fonts, clipping, text datum, battery API, heap allocation, NVS hooks and main.cpp glue compiled. Added acars_core.cpp to the CMake source list because it was absent: decoder compiled unchanged and entered libmain.a. Unused decoder routines may be removed by linker garbage collection; no receiver invokes the decoder yet. This is compilation compatibility, not decoder execution on hardware.

Preserved acars_core.hpp/.cpp and main.cpp byte-for-byte from the handoff. Preserved lazy PSRAM allocation and bounded model/controller interfaces. No new global model or receiver adapter.

## Host gates (baseline and after source fixes)

- PASS: `bash tools/test-acars-smoke.sh`
- PASS: `bash tools/test-acars-ui.sh`, including model and renderer/app ASan/UBSan, status-bar reservation and font/clip restoration checks.
- PASS: `python3 tools/check_documentation_truth.py` (0 errors, 0 warnings).
- PASS: `git diff --check`.

## Resources

- Application binary: 4,523,904 bytes (0x450780).
- App partition: 6,291,456 bytes (0x600000); free: 1,767,552 bytes (28%).
- Static D/IRAM used: 383,030 bytes; remaining: 62,362 bytes (86.0% used).
- .data: 115,232 bytes; .bss: 162,380 bytes; RAM .text: 105,418 bytes.
- ACARS object BSS: app 44 bytes + renderer 42 bytes = 86 bytes; other ACARS objects 0. Object totals are not a before/after linked-image delta.
- Binary SHA-256: 95191a3658b21130e31912b5a3921d2b167f95dc5f2f523be1d1d9751998053f.
- Comparable decoder-base build: NOT RUN. Before/after size, BSS, heap and stack delta: NOT MEASURED.
- Runtime heap, DMA headroom, stack, boot stability: NOT VERIFIED. Static linker headroom is not free runtime heap.
- ESP-IDF periph_ctrl.h emitted a literal-suffix warning; build succeeded without suppressing it.

Size command: `C:\Espressif\python_env\idf5.5_py3.14_env\Scripts\python.exe -m esp_idf_size apps/orcsdr-tab5/build-native-hosted3/orcsdr_tab5.map`.

Local diagnostics: build/acars/firmware-build.log, firmware-build-fixed.log, firmware-size.txt, acars-object-sizes.txt, smoke.log, ui-tests.log. Build outputs: apps/orcsdr-tab5/build-native-hosted3. Diagnostics and binaries remain local ignored artifacts.

## Physical Tab5 checklist

**2026-10-15 update.** The rebased branch plus the new receiver adapter (`acars_receiver.*`, `Frontend240k`, ten-phase `Decoder`, `RTL_UI TAP`, `RTL_ACARS STATUS`) was flashed to the spare Tab5 on COM39 (C6 1.4.1, a Blog V3 R820T2 dongle). Everything below marked SERIAL was driven and read over the serial console (`RTL_UI TAP x y` goes through the normal touch routing). **Nobody looked at the display**: layout, fit, overlap, readability, redraw feel and flicker are NOT RUN, and no photo or screenshot exists.

| Check | Result |
|---|---|
| Boot, `RTL_UI OPEN ACARS` | SERIAL PASS: `screen=acars route=acars`, boot reached ready. Home tile and touch open: NOT RUN |
| Header controls and status area | Settings button tap opened Settings and `OPEN ACARS` returned (SERIAL PASS). How the header draws: NOT RUN |
| Header font state after dashboard render | NOT RUN |
| All six tabs | SERIAL PASS for touch routing: tapping each tab gave view=live, aircraft, channels, activity, archive, setup. Layout and readability: NOT RUN |
| LIVE state | With the receiver running: `receiver=running live_reception=1 tune=tuned`. The "feed not connected" text with no receiver is covered by host tests only |
| DEMO | SERIAL PASS: Setup toggle gives demo=1; six scenarios cycle in order (mixed, full, empty, disconnected, paused, no clock) with receiver=disconnected and paused where expected; toggling off returns to live. List, detail, paging, raw scroll, aircraft and archive screens: NOT RUN on the panel |
| CHANNELS | SERIAL PASS: choosing or clearing a channel retunes the radio and `tuned_hz` follows once blocks arrive. A custom 130.000 MHz request was already stored on this unit's NVS (source unknown); Clear removed it |
| Preference persistence | SERIAL PASS: region changed to Europe, device reset, reopened: channel 131.725 MHz restored. The current view is not persisted (reopens on LIVE). Demo is not persisted |
| Settings and Visualizer return | Settings round trip SERIAL PASS, receiver kept running across it (block counter kept rising). Visualizer: NOT RUN |
| Leaving to Home | SERIAL PASS: the receiver stops, the stream returns to 2.4 MS/s (`RTL_DSP STATS rate=2400000`) |
| Streaming stability | 10 minute run on 131.550 MHz: 8,234 blocks, 0 stream gaps, 0 driver overruns/drops, 0 IQ pipeline drops. DSP cost about 8.0 ms mean (23.6 ms max, includes preemption) per 68 ms block, roughly 12% of one core |
| Heap / DMA | With ACARS open, `dma_free` 20,635 bytes, `dma_largest` 20,480, `dma_min` 17,359, internal free about 56.7 KB; identical after leaving to Home, so the dashboard and receiver add no measured DMA RAM (receiver and model are PSRAM, about 10 KB plus the model). Stack: `main_stack_hwm` 5,264 to 6,456 words observed. Watchdog: no reset or WDT line seen in the logs read. Long-run (hours) memory: NOT RUN |
| Photos / screenshots | NOT RUN |

### Live RF

**No real ACARS block has been decoded.** Ten minutes on 131.550 MHz produced 0 CRC-verified blocks, 10 sync candidates and 97 CRC rejects (false triggers on noise; all rejected), with the channel level flat near 0.06. Whether that dongle had a usable VHF antenna attached, and whether any aircraft transmitted on that channel in the window, is not known. This says nothing about decoder sensitivity.

### What the host tests prove about the receive chain (synthetic fixture, not a recording)

A generated AM / 1200-2400 Hz MSK signal at 240 kS/s CU8 decodes once per block at every bit phase tried (0-99 samples), with the carrier +-4 kHz off nominal, with noise (12 of 12 at sigma 0.05, no duplicates), two identical back-to-back blocks both delivered, and two seconds of noise producing nothing. Before the multi-phase decoder change the decoder had fixed bit timing and would have missed most blocks on real signals.

Decoder-owner API changes: `Decoder` now runs ten bit-phase hypotheses and `stats()` is aggregated; `Frontend240k` is new; `Model::clear_tune()` is new. Draft PR remains unmerged; nothing is pushed.
