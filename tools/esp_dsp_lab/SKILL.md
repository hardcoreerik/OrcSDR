# OrcSDR ESP-DSP source-first qualification skill

Use this checklist whenever investigating DSP, FFT, FIR, decoder bottlenecks, or ESP32-P4 assembly. It is a repeatable process, not proof that any particular binary or test was run.

1. Capture exact OrcSDR head SHA, pinned `dependencies.lock` versions, ESP-IDF/toolchain, ESP32-P4 board and silicon revision, and previous accepted/rejected DSP experiments. Keep work on a new research branch, never reset another checkout.
2. Inspect actual upstream API headers, ANSI implementation, optimized assembly, relevant issue comments, reference standards and historical OrcSDR saved-IQ evidence. Record source URLs and SHAs. Treat a hypothesis as an unproven hypothesis.
3. Run independent numerical tests against an oracle (e.g. NumPy) and original ANSI C functions compiled from a pinned upstream checkout. Include bitwise correctness where formulas are unchanged, error tolerances where justified, boundary sizes, fractional/weak tones, noise, canaries and deliberate negative controls.
4. For REA (Reverse Engineer Anything), first record Node version, REA CLI availability and native binary-analysis provider. Run `rea doctor` only in a compatible environment. Analyze linked P4 ELF/disassembly only if its RISC-V support is verified. If absent, label REA SKIPPED; never present ordinary source inspection as an REA result.
5. Inspect real linked assembly for `esp.lp.setup` loop-boundary instruction alignment and width. Use source + objdump + upstream silicon errata; a source-only inspection cannot verify final instruction addresses.
6. Build standalone ESP-IDF probe, not a new `app_main` inside OrcSDR. Compare ANSI and optimized FFT/FIR on identical data, actual P4 revision, internal vs PSRAM placement, concurrent tasks/interrupt pressure; log complete arrays/CRC decisions and P50/P95/P99, drops, heap and clock.
7. Test real RF/saved IQ and normal OrcSDR UI/audio/USB pipeline with A/B baselines. Do not promote an optimization that changes decoder truth or creates backlog. Keep focused modules, `main.cpp` small and M5GFX/status-bar standards unchanged.
8. Deliver timestamped source map, reproducible commands, log in same folder as runner, raw/summarized machine-readable results, hashes, risk register, specific pass/fail gates and explicit list of what was NOT tested. Do not merge production code before hardware acceptance.

Active example: `codex/esp-dsp-rf-power-cache-lab`. The repo includes a 360-vector exact channel-power/99%-OBW host test and CI; standalone full FFT/P4 probes are separate sandbox research artifacts. Current production promotion remains pending a native firmware build and real Tab5 qualification.
