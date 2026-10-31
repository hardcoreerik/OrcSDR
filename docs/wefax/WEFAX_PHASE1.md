# OrcSDR Weather Fax — Phase 1 host-core evidence

Date: 2026-10-28. Development branch: `chatgpt/wefax-decoder-development`.

## Scope and evidence

This is a clean-room, receive-only WEFAX **manual-frame** baseline. It is a portable C++17 component with no display, ESP-IDF, SD, heap, UI, or hardware dependencies. Decoder takes either 12,000-sample/s signed 16-bit PCM or 48,000-sample/s PCM, mixes around the nominal 1,900 Hz audio center, estimates instantaneous FM frequency, and rasterizes grayscale into caller-owned rows. `set_row_sink` runs synchronously. Manual start requires the caller to know the image line boundary (e.g. from phasing); this is not a claim of automatic fax acquisition.

- Nominal tone mapping: black 1500 Hz, white 2300 Hz, linear grayscale between them.
- Output widths: IOC 576 -> 1810 pixels, IOC 288 -> 905 pixels; geometry is rounded from π × IOC (independent from the signal's usable information bandwidth).
- Modes supported in this baseline: 60, 90, 120, 240 lines/min; 288 and 576 IOC.
- Bounded rows: user-supplied cap (default 2400); overflow cannot grow an image buffer in this core.
- No long-lived image allocation: one 1810-byte pixel row; consumer owns storage and decides whether/how to save.
- 48k PCM reduced to 12k by four-sample averaging. This is intentionally simple, not a qualified anti-alias filter.
- Detector: complex 1900 Hz downmix, two low-pass poles, differential phase; fixed filter is a baseline for later comparison, **not proven optimal**.

## How to run/test

**Linux / macOS / Git Bash** from the repository root:

```sh
bash tools/wefax_host_test.sh
```

The script builds a temporary executable, tests input combinations, prints per-case MAE, and saves the run log to `tools/wefax_host_test.log` in the repository working folder. It sets `WEFAX_TEST_PGM` to write `/tmp/orcsdr_wefax_reference.pgm` on Unix-like hosts as a generated output image. Do not commit those generated test outputs.

**PowerShell on NewcorePC**, when `g++` is available:

```powershell
cd F:\AI\OrcSDR
g++ -std=c++17 -O2 -Wall -Wextra -Werror -Iapps/orcsdr-tab5/ui tools/wefax_core_test.cpp apps/orcsdr-tab5/ui/wefax_core.cpp -o "$env:TEMP\orcsdr-wefax-test.exe"
& "$env:TEMP\orcsdr-wefax-test.exe" 2>&1 | Tee-Object -FilePath .\tools\wefax_host_test.log
```

Run from whichever Git working tree contains this branch. Image output is optional: set `WEFAX_TEST_PGM` to an absolute destination path before running the test on Windows. The assertions and log work without an image path.

## Observed host results

Host: Debian, g++ 14.2.0, `-O2 -Wall -Wextra -Werror`, deterministic synthetic continuous-phase audio. 9 rows per case; MAE over 8 complete pattern rows (excluding 25 border pixels). These are PCM-simulation results, not off-air results:

| Format | PCM | Synthetic SNR | MAE (0–255) |
|---|---:|---:|---:|
| IOC 576 / 120 LPM | 12000 Hz | 80 dB | 6.280 |
| IOC 576 / 120 LPM | 12000 Hz | 18 dB | 8.918 |
| IOC 576 / 120 LPM | 48000 Hz | 80 dB | 6.390 |
| IOC 288 / 60 LPM | 12000 Hz | 80 dB | 5.644 |
| IOC 576 / 240 LPM | 12000 Hz | 80 dB | 7.996 |
| IOC 288 / 90 LPM | 48000 Hz | 22 dB | 6.002 |

Tests additionally check chunk-boundary invariant bit-identical output, silence, invalid format rejection, bounded row cap, and abort without fabricating rows.

## Known gaps (do not claim these features yet)

1. No start/stop APT tone detection (300/675/450 Hz), automatic phasing/line timing, slant correction, or clock drift estimation.
2. No AFC or RF tuning offset correction; nominal 1900 Hz is assumed.
3. No detection/repair of dropped PCM samples or transmission interruptions; audio gaps shift the raster unless an upstream timing layer provides compensation.
4. No file archival, station schedule, image gallery, UI/dashboard, remote API, or device driver connection.
5. No calibrated ESP32-P4 cycle count, PSRAM, stack-usage or hardware tests. No real WEFAX reception or competitor head-to-head scores.
6. Native output width is a geometry decision, not proof of recovered optical resolution; LPF and SNR limit detail.
7. The decoder processes PCM synchronously; it must run on a bounded consumer task, NOT directly in OrcSDR's time-critical RTL/USB callback.
8. At the 48k entrypoint, use either 48k or 12k per decoder session; do not interleave sample-rate methods.

## Immediate next milestones

1. Build deterministic *audio-framed* tests with actual APT start, 60 phasing lines and stop, including capture that begins mid-transmission.
2. Implement robust APT and phasing lock as separate, testable acquisition components (explicitly label tentative versus confirmed locks).
3. Add fractional line-clock estimation and gap metadata from the existing OrcSDR audio handoff; record SNR/quality without equating heuristics to probability.
4. Replay real reference WAVs and compare exactly identical inputs across fldigi, Isobar, G7IZU and OrcSDR; preserve raw images separately from repaired/enhanced copies.
5. Benchmark on ESP32-P4, then integrate with `main.cpp` receiver/session ownership and the adaptive display framework. Validate Blog V4 HF reception on actual hardware.

## Primary RF reference

Australian Bureau of Meteorology HF fax technical details: https://www.bom.gov.au/marine/tech_voice_fax.shtml

This source documents 1900 Hz center, 1500/2300 image tones, 120 LPM, IOC 576 and the start/stop/phasing sequence. The test encoder and C++ implementation were written independently. No third-party decoder code or proprietary binaries were copied into OrcSDR.
