# Codex task: make the OrcSDR digital-voice decoder and vocoder dramatically cheaper (CPU and memory)

You are working in the OrcSDR repository (ESP32-P4 firmware for the M5Stack Tab5). Branch to start from:
`feature/dmr-digital-voice-foundation` (the owner's local branch is `local/digital-voice-integration`, head `62558be1` or later). Create your own new branch from it, for example `codex/dv-efficiency`. Never push to `main`, never merge, never open a PR unless the owner says so. Read `AGENTS.md`, `CHANGELOG.md` (newest entries first), `docs/UI_RENDERING_STANDARD.md` and `apps/orcsdr-tab5/ui/AGENTS.md` before you change anything. Every change needs a CHANGELOG entry that says plainly what was measured and what was not.

## Why this matters (measured on the real Tab5, 2026-11-27)

While a Digital Voice scan (SCAN or MONITOR) runs, the decode worker `dv_rx` (`apps/orcsdr-tab5/ui/dv_radio_task.cpp`: pinned to core 0, priority 2, 12 KiB PSRAM stack) uses 71 to 82 percent of core 0. The Arduino main loop (priority 1, also core 0) then gets about 3 percent, so the screen stalls about 850 ms at a time and touches are lost. Core 1 is about 80 percent idle (the DSP task `rtl_dsp` uses 17 to 22 percent there). The worker's sample ring (`kSampleRing = 32768` samples, 256 KiB PSRAM) overflows about twice a second (`ring_drops` rises), so decoding is also losing samples.

The DMR decoder cost was **4.0 microseconds per 48 kS/s sample** in the slow state against about **0.9 microseconds** in a good run on the same firmware (`us_dmr` delta divided by `pairs` delta from `RTL_DV STATUS`). The decoder state is in internal RAM in both runs (`dec_internal=24144`). Why the cost differs between runs is not known. Candidates to test, not conclusions: instruction-cache or PSRAM-bandwidth contention (the display, the IQ ring and the voice synthesis all use PSRAM), data-dependent decoder search modes on strong analog carriers, preemption by sdio/Wi-Fi tasks on core 0 inflating the measured wall time, a different code path when a call is present.

Voice synthesis is also expensive: `RTL_DV AUDIO` reports `synth_us=55442/133191/91714` (average / maximum / last, microseconds) per voice call in the `mbelib`-based vocoder, with `pump_max_us` up to 83 ms.

Internal RAM is almost full after boot (`RTL_DRAM_BUDGET` lines show about 15 to 22 KB free internal and a 16 KB largest block). A growth of a few hundred bytes of internal statics has boot-looped a unit before. New buffers must go in PSRAM, and PSRAM-heavy designs must be measured because the display shares PSRAM bandwidth.

## What to build

Goal: cut the decoder and vocoder work by a large factor (aim for the worker at or below 25 percent of one core in the worst measured state, the vocoder at least 2 times cheaper per call, and fewer bytes of RAM) **without** lowering decode accuracy or audio quality. Concrete directions, in the order I would measure them:

1. **Profile before touching code.** Use the tools below to get a per-stage budget: channelizer (`us_ch`), DMR (`us_dmr`), NXDN48/96 (`us_n48`, `us_n96`), voice synthesis, ring handling, and the worker's wall time against its CPU time. Establish whether the 4 times slowdown is cache/memory effects or work done. Report numbers before and after every change.
2. **Do less work per sample.** Candidates: decimate earlier and run the DMR/NXDN frontends at the lowest rate that keeps the bit error rate unchanged; skip the NXDN decoders when the plan mask does not need them (the MONITOR plan knows the protocol per channel; `plan_mask` already carries it); gate the expensive acquisition/search path behind a cheap energy or sync pre-detector; share the first filter stage between protocols; process in blocks that stay in cache; use fixed-point or the P4's RISC-V SIMD/packed instructions where they help; move hot filter taps and histories into internal RAM or use smaller coefficient tables.
3. **Vocoder.** The AMBE+2 decode (`components/orcsdr_vocoder`, built on `components/mbelib`) costs 55 to 133 ms per superframe. Find where the time goes (spectral/harmonic synthesis, FFT or sine-table work, float versus fixed point, per-frame allocations), then cut it: precomputed tables in flash/PSRAM chosen by measurement, float32 with the P4's FPU used consistently, avoiding `double` and libm calls in inner loops, less per-frame copying. Also see whether the synthesis can run on core 1 (which has headroom) at a priority below the DSP task, without letting the display stall.
4. **Memory.** Shrink the 256 KiB sample ring if the worker keeps up; shrink or share `Decoders` (24 144 bytes in internal RAM) and `State`; drop any copy of data that already exists elsewhere. Report the before and after of internal and PSRAM free (`RTL_MEM STATUS`).
5. **Scheduling.** If the worker cannot be made cheap enough, make it fair: lower its priority below the main loop, or add a cooperative yield so the main loop is never starved for more than about 20 ms, and measure that the sample drops do not rise. Do not hide drops: keep the counters honest.

## Rules you must follow

- **Clean-room and licences.** No GPL code goes into the repository. You may read GPL projects (DSD-FME, OP25, DSDcc, etc.) only for behaviour and ideas, never copy code, comments or tables. `mbelib` is already in the tree under its own licence; do not widen what it does or add new third-party vocoders without asking the owner.
- **Do not break correctness.** The existing host tests and corpus must still pass with identical decode results: `tests/digital_voice_live` (build with CMake in WSL: `cmake -S tests/digital_voice_live -B ~/dvlive && cmake --build ~/dvlive`), `tests/digital_voice_corpus`, the vocoder tests (`dv_voice_tests`, 168 checks), and `tests/digital_voice_dashboard_preview`. Add a regression test that proves the optimised path gives bit-identical (or, for audio, measured-equivalent: report SNR/error in dB) output against the old path on the recorded corpus. State exactly what the tests compare.
- **Driver independence.** The RTL-SDR driver never depends on OrcSDR. Apply the driver's own limits, never new ones of your own.
- **Do not grow `main.cpp`.** New code goes in its own module with a narrow interface.
- **Screens.** No flicker, ever: paint only changed widgets and never full-clear on a counter (see `docs/UI_RENDERING_STANDARD.md`). Do not change the look or behaviour of any dashboard without the owner's yes.
- **Hardware.** The owner's dev unit is the Tab5 on **COM17** (M5Stack Tab5, ESP32-P4). **COM39 and COM24 are different devices; never flash or open them.** Do not flash or run device tests unless the owner has said you may in this session; if you may, use `idf.py -B build-native-hosted3 -p COM17 flash` from `apps/orcsdr-tab5` after sourcing ESP-IDF 5.5.4 (`C:\Espressif\frameworks\esp-idf-v5.5.4\export.ps1`, `IDF_PYTHON_ENV_PATH=C:\Espressif\python_env\idf5.5_py3.14_env`). Never leave a scan running unattended on the unit: a scan right after boot makes the screen unusable. **Wait about 4 minutes after any flash before judging smoothness**, and measure scanning and idle separately.
- **Honest reporting.** Say what was measured, on which build, and what was not. A host benchmark is not a device result; a good run is not proof of a fix. Report failures with their output.

## How to measure on the device (already in the repo)

Serial helper: `artifacts/digital-voice-integration/benchmarks/tab5_bench_lib.py` (`connect()`, `run(c, cmd, window)`, `find`, `kv`; opening the port this way does not reset the board). Start a scan: `RTL_UI OPEN DIGITAL_VOICE`, `RTL_DV MONITOR ALL`, `RTL_DV SCAN START` (retry while it says REFUSED right after boot), stop it with `RTL_DV SCAN STOP`.

- `RTL_DV STATUS`: `us_ch`, `us_dmr`, `us_n48`, `us_n96`, `pairs`, `blk_us_avg`, `worker_busy_pm` (per mille), `ring_drops`, `ring_max`, `dec_internal`, `state_bytes`.
- `RTL_DV PROBE START | DUMP | STOP`: which task runs on each core, every millisecond, with each task's share and longest unbroken run (this found the worker starving the main loop).
- `RTL_DV RENDER`: how often the main loop reaches the DV screen, longest gap, per-part tick times, repaint cost per view.
- `RTL_DV AUDIO`: voice engine counters, `synth_us`, `clipped`, `underruns`, `rec_*`.
- `RTL_MEM STATUS`: internal and PSRAM free and largest blocks.
- Scripts: `benchmarks/worker_load_timeline.py`, `boot_stall_timeline.py`, `stall_count.py`, `stall_ab.py`, and the firmware's own `RTL_MAIN_STALL stage=loop_gap` lines (a loop gap of 500 ms or more).
- Real recordings for regression: `artifacts/digital-voice-integration/` (corpus, captures; third-party call audio stays out of git).

## Definition of done

A branch with: the profile you started from; each optimisation as its own commit with before/after numbers; regression tests that pass; the worker's share of core 0 and the main-loop stall count (use `stall_count.py` during a 30 second scan, 4 minutes after boot) before and after; vocoder time per call before and after; internal and PSRAM memory before and after; a CHANGELOG entry per change; and a short list of what you could not verify. Stop and ask the owner before any change that alters decode results, audio output, the file formats on the SD card, or the on-screen behaviour.
