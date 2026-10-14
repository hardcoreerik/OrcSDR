# ESP-DSP / RF Analysis linear-power cache — qualification record

Status: experimental research branch only. Not hardware-verified or production-accepted.

## Exact provenance

- OrcSDR main baseline: `789eede1a37d1f235bd185ca2d014a17ab2c6c1b` (2026-10-13).
- Pinned ESP-DSP version: 1.8.2 in `apps/orcsdr-tab5/dependencies.lock`.
- Changed module: `apps/orcsdr-tab5/ui/rf_analysis.cpp`; no change to main.cpp, LoRa, DSP demodulators, display, M5GFX or status bar.
- P4 HWLOOP investigation is separate: https://github.com/espressif/esp-idf/issues/19025 . This branch does not change the FFT kernel.

## Optimization and ownership

The existing spectrum bin loop computes `powf(10.0f, db / 10.0f)` for the moving average and later repeats that conversion for channel power and both 99%-OBW passes. A worker-local float[kMaxBins] PSRAM cache reuses that exact already-calculated float value in the latter loops. Floating-point accumulation order, constants, measured bandwidth, FFT and window math are unchanged. Allocation failure is checked alongside the original worker buffers. The cache is never shared with other tasks.

## Evidence and limits

- Host mathematical A/B: `tools/esp_dsp_lab/rf_power_cache_regression.c` runs 360 deterministic inputs from six bin counts (256 to 8192), flat/noisy/strong spectral cases and several bandwidth and Hz-per-bin combinations.
- GCC `-O2 -fno-fast-math -ffp-contract=off` on Linux: 360/360 exact float channel-power and integer occupied-bandwidth comparisons passed in assistant sandbox.
- A separate isolated host microbenchmark reported approximately 2x faster power/OBW math, with the new conversion cache included in timing. It is NOT an ESP32-P4 performance result.
- The test compiles its isolated mathematical model, not the full firmware source. An ESP-IDF build has NOT been performed in this sandbox.
- Tab5 hardware runs, saved-IQ replay, display/USB/audio latency, heap, and decoder performance: NOT VERIFIED.

## Gates before promotion

1. Build this branch on the supported ESP-IDF 5.5.4 native Tab5 toolchain with the embedded C6 image, capture log and ELF SHA256.
2. Replay identical saved live IQ on the baseline and this branch. Compare live/average/peak arrays, noise, SNR, channel-power dBFS, 99% occupied bandwidth and frequency marker across all supported FFT sizes, with exact equality where calculations are unchanged.
3. On an approved spare Tab5 run real RF/UI/USB/audio workloads. Record RF-worker P50/P95/P99 and max, task CPU utilization, free internal RAM/PSRAM, throughput, errors, data drops and watchdog resets.
4. Accept only if numerical outputs match, scheduling and resource use remain sound, and measurable P4 performance improvement is shown. Otherwise revert the single focused commit.
5. Independently investigate the LoRa current ARP4 call versus historical ANSI correctness validation. Do not treat this optimization as a fix for ESP32-P4 hardware-loop errata.

Evidence levels: host arithmetic PASS; hardware NOT TESTED; firmware build NOT TESTED; release qualification PENDING.
