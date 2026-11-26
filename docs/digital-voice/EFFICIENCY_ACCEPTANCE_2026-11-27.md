# DV decoder efficiency: physical acceptance on the Tab5 (2026-11-27)

Measured on the owner's M5Stack Tab5 (ESP32-P4, COM17), RTL-SDR dongle, 240 kS/s stream, decoder state in PSRAM (`kDecodersInInternalRam = false`). Each run is a fresh flash, then 240 s of boot stabilisation, then idle for 30 s, then five single-channel test points of 8 s settle and 30 s window (`artifacts/digital-voice-integration/benchmarks/accept_measure.py`, raw results in `benchmarks/accept/*.json`). Candidates come from the independent sandbox branch `agents/dv-efficiency-20261126` (corrected head 72a2e178; the vocoder experiment 431086be is NOT integrated).

## Method and what the numbers mean

- Test points (one channel each, one decoder each): `analog` 162.550 MHz NOAA weather (strong analog FM) with the DMR decoder only; `noise` 163.000 MHz (nothing) with the DMR decoder only; `dmr` 152.1575 MHz DMR control channel (constant traffic), DMR only; `nxdn48` and `nxdn96` 463.250 MHz (known NXDN48 system) through each NXDN decoder (CPU and memory comparison, not a decode test).
- us/sample = wall microseconds spent inside the decoder divided by the samples ACTUALLY delivered to that decoder (new counters `smp_dmr`, `smp_n48`, `smp_n96`: 48 kS/s for DMR and NXDN96, 24 kS/s after the half-rate decimator for NXDN48). The older `pairs` counter counts raw 240 kS/s input pairs; per raw pair the numbers are about five times smaller (an earlier note of 1.14 us per sample for the DMR decoder was per raw pair; the comparable figure is about 5.4 us per delivered sample).
- Wall time is not CPU time: preemption by higher-priority tasks inflates it. The per-task shares come from the scheduler probe (`RTL_DV PROBE`, a task on the other core samples which task runs, every millisecond) and are the CPU view.

## Results

| Run | analog DMR us/smp | noise DMR us/smp | dmr DMR us/smp | NXDN48 us/smp | NXDN96 us/smp | worker busy (analog / noise / dmr / n48 / n96, permille) | receiver state bytes |
|---|---|---|---|---|---|---|---|
| Baseline (be88bce4 build) | 10.54 | 10.55 | 5.45 | 15.20 | 15.35 | 489 / 497 / 259 / 360 / 713 | 40040 (earlier status line) |
| Candidate A (11bffbfb, DMR acquisition) | 4.86 | 4.87 | 4.70 | 14.93 | 15.12 | 225 / 229 / 220 / 356 / 712 | 40040 (earlier status line) |
| Candidate B (5d16a9e8, NXDN storage) | 10.54 | 10.55 | 5.05 | 14.85 | 15.21 | 489 / 497 / 246 / 352 / 713 | 33800 |
| A + B | 4.87 | 4.94 | 4.76 | 15.03 | 13.65 | 226 / 233 / 224 / 356 / 645 | 33800 |

Per run, all five test points and idle: main-loop stalls 0 (firmware `RTL_MAIN_STALL loop_gap`), ring drops 0, longest DV screen tick gap 18 to 21 ms at idle. Channelizer 0.85 to 0.87 us per raw input pair, about 22 percent of core 1 (`rtl_dsp` share 223 to 228 permille) in every run. Internal RAM free 34.5 KB with a 15.4 KB largest block at the start of every run; PSRAM free about 13.5 MB (largest block 13.37 MB, 12.85 MB at the end of the A+B run after the probe tables were allocated).

Task share of core 0 held by `dv_rx` (permille), analog / noise / dmr / n48 / n96: baseline 509 / 509 / 255 / 364 / 736; A 232 / 233 / 223 / 364 / 727; B 509 / 508 / 260 / 363 / 730; A+B 232 / 233 / 225 / 364 / 656. Stack free high-water marks are in the JSON (`probe[...].stack_free_hwm`, unit as reported by `uxTaskGetStackHighWaterMark`).

## Findings

- **Candidate A (DMR acquisition screen) works on the target and beats its host claim.** On analog FM and on noise, where the decoder spends its time searching for sync, DMR cost fell from about 10.5 to 4.9 us per delivered sample (-54 percent) and `dv_rx` from about 51 to 23 percent of core 0; on a real DMR signal 5.4 to 4.7 us (-14 percent). The sandbox reported 40 to 46 percent less host CPU on noise and analog workloads. NXDN is unaffected, as expected.
- **Candidate B (compact NXDN storage) saves memory and changes no speed.** Receiver state 40 040 to 33 800 bytes (-6 240; the sandbox host figure was -6 880). NXDN48 and NXDN96 cost per sample is unchanged within run-to-run noise (14.9 and 15.2 against 15.2 and 15.3). The decoder state is in PSRAM, so internal RAM free did not change (34 539 to 34 607 bytes).
- **A and B together** keep A's DMR gain (4.87 us on analog and noise). The NXDN96 point read 13.65 us against 15.2 to 15.3 in the other runs (-11 percent) and `dv_rx` 656 against 730 permille; this is a single run and is not explained by anything B or A changes in the NXDN96 path, so it is recorded as an unexplained observation, not a gain.
- DMR on the control channel read 5.05 us in the B-only run against 5.45 in the baseline although B does not touch the DMR decoder: traffic on that channel differs between runs, so the `dmr` point is good to about 0.5 us.

## Not done / not verified

- **Decode equivalence on the device.** The host corpus, live-chain, voice (168 checks), data-log and dashboard suites pass for A alone and for A+B (7 of 7 and 5 of 5); the sandbox reports bit-identical host output for A. A device-side comparison of decoded events and CRC-valid counts between builds was NOT done.
- **An established voice call and voice synthesis timing.** No call occurred during the runs; `synth_us` is recorded in the JSON only as the counters stood, not as a controlled measurement. Voice synthesis (the vocoder) is unchanged: the sandbox's vocoder change is excluded.
- **The slow acquisition state of the earlier internal-RAM placement** is not reproduced here (the decoder state is in PSRAM); these runs are the PSRAM configuration only.
- Single run per configuration; no repeat for variance beyond the cross-run noise visible in the `dmr` point and the NXDN96 observation above.
- Ring size, task priorities, task cores and UI behaviour were not changed.

## Failed or abandoned experiments in this session

- Decoder state in internal RAM (the old default): 3.97 us per DMR sample per raw pair, worker 82 percent, ring drops, main loop starved; reverted to PSRAM (`019b4506`).
- Mis-scaled comparison: an earlier note compared DMR cost per raw input pair (0.9 and 1.14 us) with a figure per delivered sample; corrected above.

