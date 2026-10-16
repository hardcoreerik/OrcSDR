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

**First real off-air ACARS decoded, 2026-10-15, spare Tab5 on COM39, 131.550 MHz.** After about 16,000 blocks (roughly 18 minutes) of listening the receiver held two CRC-verified blocks, both from tail N37267, flight UA1227, label Q0, text `S31AUA1227`, stamped 04:13:13 and 04:13:23 on the device clock (UTC, shown because the Tab5 clock was valid). Both were verified without bit repair. The two blocks are ten seconds apart with identical content, consistent with a retransmission. Evidence: a photograph of the LIVE tab supplied by the user; the serial counters at the time read `crc_ok=2 corrected=0 crc_rej=161`.

This is two blocks. It establishes that the chain (dongle, front end, decoder, model, screen) works on a real signal. It says nothing about sensitivity, miss rate or false-block rate: no second decoder ran alongside, the antenna was a 144/430 MHz amateur-band VHF/UHF whip used indoors (reported by the user), not an ACARS-tuned 22 inch vertical, and the ten-minute run just before it (0 blocks) shows how sparse traffic on one channel is.

**Later photos (same run, ACTIVITY tab and message detail pages).** The device held 8 CRC-verified blocks, 0 repaired, when the ACTIVITY page was photographed. The device's own aircraft counter read 1 at that time; the photographed blocks are N37267 / UA1227 on 131.550 MHz. Four are label H1, mode 2, block IDs 0, 1, 2, 3, received 04:17:15, 04:17:42, 04:17:49 and 04:17:57 UTC, each ending ETB with 236 raw bytes. The texts begin `D03A`, `D03B`, `D03C`, `D03D` followed by `UA1227#DFB`, i.e. consecutive blocks of one multi-block message, and block 0 carries `HYDN37267 ... KSFOKPDX` (origin and destination airport codes) followed by the aircraft's performance-data text. Block IDs 0 to 3 are consecutive, so none of those four was missed. The 27 s gap between block 0 and block 1 is longer than the roughly 8 s spacing of the later blocks, which suggests some transmissions in between were not decoded. That is an inference, not a measurement. The message ends with ETB, so more blocks may have followed.

The ACTIVITY tab read: sync candidates 29, CRC verified 8, repaired 0, CRC rejected 225, framing rejected 0, buffer overflows 25, bits 3,581,545, "verified per sync candidate 27%". Caveat on those numbers: `Decoder::stats()` reports unique blocks for CRC verified but the busiest single bit phase for the other counters, so the 27% ratio and the reject count are approximate. They are not clean per-attempt rates. The reject count also includes noise-triggered frames.

**Label H2 blocks from a second aircraft.** A later screen recording of the message detail page (same session, about 04:21 UTC) shows label **H2** blocks, ETB-ended, 236 raw bytes, CRC VERIFIED, from a different flight than UA1227. The flight and tail were read from low-resolution video frames and are not confirmed: the frames show `N326NV` and a flight string that reads `G41707` or `041707` (an Allegiant G4 flight number is the likelier reading; an earlier note here said UA1707, which was a misreading). The text is wrapped escaped data with `<CR><LF>` separators. So the receiver decoded at least two labels (H1 and H2) from at least two aircraft, which also corrects an earlier note here that all blocks were from one aircraft.

On-panel observations from the photos (not a design review): the LIVE list, detail page (NEWER, OLDER, UP, DOWN, BACK), tab bar and status panel are legible and consistent with the model; the LIVE column header `INTEGR` and the `DOWN` button label (`DO...`) are clipped, and two ACTIVITY tile captions end in an ellipsis. Whether the list repaints by itself as blocks arrive was not observed: the photos were taken with a detail page open. Flicker and redraw speed: NOT RUN.

Observed on the photo only, not checked further: the right-hand column header on the LIVE list reads `INTEGR`, clipped at the panel edge. Flicker cannot be judged from a still.

### Flicker and redraw speed (2026-10-15, COM39)

A video from the user showed the whole content area blanking and redrawing every few hundred milliseconds on a static page. Cause: the decoder reports its demodulated-bit counter about twice a second, and every stats change bumped the snapshot revision, so each page repainted in full for nothing. Three changes, measured on the device with `RTL_ACARS STATUS`:

- A bits-only stats change no longer bumps the revision (only the ACTIVITY page, which shows the counter, refreshes it once a second). Host test: `test_bits_only_stats_do_not_repaint`.
- Frames are composed off-screen in a PSRAM sprite (the cloud-session commit 23438e3, cherry-picked) and compared with the previous frame (`acars_dirty_rects`); only changed 160x34 tiles are pushed. Measured: 1 to 2 ms per partial push, about 28 ms to compose, about 28 ms to diff.
- Pushing a whole sprite is slow on this panel: 647 to 818 ms (about 1.2 M pixels/s). The user reported "touch responsive, but redraw slow, right to left". Whole-frame changes (page changes) are therefore drawn straight onto the display with the native primitives: 78 to 89 ms measured.

The user reported on the panel that the flicker is gone. That is the user's observation; no measurement of flicker exists beyond the paint timings above. Not done: pushing only the changed tiles for large changes, and a pixel-modelling host stand-in, so the partial-push path is covered by `acars_dirty_rects_tests` plus the device run, not by the renderer host tests.

### Pre-flicker and flicker-fixed builds compared (2026-10-15)

To rule out the flicker work as a cause of quiet periods, the commit before it (`cde4cac6`, plus the self-test) ran on the main Tab5 (COM17) and the flicker-fixed branch on the spare Tab5 (COM39), both tuned to 131.550 MHz. `acars_core.*` and `acars_receiver.*` are byte-identical between the two (checked with `git diff` from the commit before the flicker work).

- `RTL_ACARS SELFTEST` (a known synthetic signal through a private decoder): **10 of 10 cases pass, 0 missed, 0 duplicates, on both builds** (606,480 samples in about 290 ms of DSP time, about 480 microseconds per 1,000 samples, i.e. roughly half real time for this path). The fixture is synthetic; the result says nothing about real-signal sensitivity.
- Both builds were silent for about 20 minutes with no traffic overhead (level flat, 0 CRC-verified blocks), then both decoded real blocks later the same evening: COM17 held 1 verified block and COM39 held 4, 0 repaired, 0 stream gaps on either. The two units have different antenna placement and channel level (COM17 about 0.13, COM39 about 0.03), so the counts are not a quality comparison.

### What the host tests prove about the receive chain (synthetic fixture, not a recording)

A generated AM / 1200-2400 Hz MSK signal at 240 kS/s CU8 decodes once per block at every bit phase tried (0-99 samples), with the carrier +-4 kHz off nominal, with noise (12 of 12 at sigma 0.05, no duplicates), two identical back-to-back blocks both delivered, and two seconds of noise producing nothing. Before the multi-phase decoder change the decoder had fixed bit timing and would have missed most blocks on real signals.

Decoder-owner API changes: `Decoder` now runs ten bit-phase hypotheses and `stats()` is aggregated; `Frontend240k` is new; `Model::clear_tune()` is new. Draft PR remains unmerged; nothing is pushed.
