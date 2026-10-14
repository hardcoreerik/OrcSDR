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

Every item below is NOT RUN. The user explicitly prohibited flashing pending a new instruction. No physical or live RF validation is claimed.

| Check | Result |
|---|---|
| Boot, aviation Home tile, touch open, RTL_UI OPEN ACARS | NOT RUN |
| Shared brand/battery/Home/Mute-volume/Visualizer/Settings header and empty status area | NOT RUN |
| Header font state after dashboard render | NOT RUN |
| All six tabs: layout, fit, overlap, arm-length readability and touch | NOT RUN |
| LIVE receiver-feed-not-connected and no-receiver-feed notice | NOT RUN |
| DEMO chip, list/detail/paging/raw scroll/aircraft/archive, six scenarios and exit cleanup | NOT RUN |
| CHANNELS request-only selection and NOT TUNED state | NOT RUN |
| Preference persistence across reboot; demo remains nonpersistent | NOT RUN |
| Settings and Visualizer return; Home cleanup | NOT RUN |
| Redraw latency, touch responsiveness, idle redraw and background RTL streaming stability | NOT RUN |
| Watchdog, heap warnings, long-run memory, photos/screenshots | NOT RUN |

Default LIVE and synthetic DEMO separation passed host checks only. Live RF: NOT RUN; no ACARS receiver adapter exists. Decoder-owner API changes: none proposed from this build. Draft PR remains unmerged.
