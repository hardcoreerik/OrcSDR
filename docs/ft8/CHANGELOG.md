# FT8 Native Decoder Changelog

This file tracks changes made specifically by the native OrcSDR FT8 decoder workstream. The main branch did not contain a repository-wide CHANGELOG.md when this workstream started.

## 2026-10-07 — Phase 0 research

- Created the native FT8 decoder research branch from current OrcSDR main.
- Added DECODER_RESEARCH.md covering the current FT8 protocol, existing decoder strategies, embedded constraints, ESP32-P4/Tab5 capabilities, clean-room provenance, proposed OrcSDR architecture, risks, and benchmark plan.
- Recorded that the DecoderBackend and Decode UI seam is currently on the separate FT8 UI sandbox branch rather than main; no UI work was copied into this branch.
- Established a Phase 0 stop gate: no decoder implementation begins until the owner approves the research decisions.
- No firmware/runtime code, transmit code, DSP changes, or hardware-validation claims were added.


## 2026-10-07 — Phase 1 slice 1: protocol primitives

- Owner approved the Phase 0 workflow; implementation remains confined to `codex/ft8-native-decoder-research` and draft PR #172.
- Added a pure C++17 FT8 protocol core with no ESP-IDF dependencies: protocol sizes, CRC-14, standard callsign c28 encoding, 10/12/22-bit callsign hashes, FT8 Gray tone mapping, and Costas/data-symbol framing.
- Added host regression runners for Linux/WSL and PowerShell plus optimized and ASan/UBSan coverage.
- Cross-checked the independent implementation against the QEX authors' explicitly public-domain helper resources: 100 CRC vectors, 100 callsign-hash vectors, and 100 standard-callsign encodings; 300/300 matched.
- Added `docs/ft8/PHASE1_IMPLEMENTATION.md` as the implementation/provenance notebook.
- This slice is not wired into the Tab5 firmware build, changes no receiver DSP, adds no transmit path, and makes no hardware-verification claim.


## 2026-10-07 — Phase 1 slice 2: LDPC definition and correctness

- Added a pure C++ FT8 LDPC(174,91) systematic encoder and sparse parity/syndrome checker.
- Stored compact protocol constants generated from the QEX authors' explicitly public-domain `generator.dat` and `parity.dat` resources.
- Cross-checked the generator matrix against the independently supplied parity-check matrix using all 91 message basis vectors and 1,000 deterministic pseudo-random messages.
- Verified every one of the 174 single-bit codeword corruptions produces a nonzero syndrome of weight three, matching the published column weight.
- Added a fixed public-matrix-derived parity vector and extended optimized + ASan/UBSan host CI.
- No soft LDPC decoder, RF/DSP integration, firmware build integration, transmit path, or hardware-validation claim is included in this slice.


## 2026-10-07 — Phase 1 slice 3: normalized-min-sum LDPC decoder

- Added a pure C++ normalized-min-sum LDPC soft decoder with fixed-size caller-owned scratch and early syndrome termination.
- Refactored the public-domain FT8 sparse parity graph into one shared internal definition; NMS check adjacency is derived at compile time.
- Added deterministic optimized + ASan/UBSan tests including the 255-iteration wrap guard.
- Added a reproducible BPSK/AWGN normalization sweep; alpha 0.80 is the provisional synthetic-channel baseline, subject to re-test with real FT8 tone-derived LLRs.
- Documented that LDPC convergence is not a valid FT8 decode; CRC and legal message plausibility remain mandatory downstream gates.
- Host workspace measured 4,872 bytes; no Tab5/P4 performance or hardware-verification claim is made.


## 2026-10-07 — expanded Phase 0: FT8 / FT4 / JS8 and integrated UI baseline

- Preserved the pre-rebase decoder checkpoint at `codex/ft8-native-decoder-pre-ui-rebase`.
- Rebased/reconstructed the decoder workstream on `claude/ft8-ui` and retargeted draft PR #172 to that branch; `main` remains untouched.
- Merged the UI branch's FT8 model/Hunter/OrcDial host tests with the native codec/LDPC/NMS regression runner. Documentation Truth, FT8 Native Decoder Core, and FT8 RX core CI all passed on the integrated checkpoint.
- Expanded research from FT8-only to a ModeProfile framework for FT8, FT4, and JS8.
- Verified FT4's 7.5 s period, 576 samples/symbol at 12 kHz, 20.8333 baud/tone spacing, 105-symbol framing, 83.3 Hz nominal bandwidth, shared LDPC(174,91)/CRC-14 family, four Costas arrays, and FT4 payload XOR transform from primary WSJT-X/QEX sources.
- Recorded stable JS8 Slow/Normal/Fast/JS8 40 profile timing and framing behavior from the current JS8Call guide, with source-only protocol research confirming the 75-bit + CRC-12 / N=174,K=87 frame family. JS8 60 remains experimental/disabled.
- Added a backwards-compatible decoder seam proposal: optional mode selection, millisecond slot timing, mode/provenance tags, and later bounded JS8 message assembly output.
- Audited the current receive/audio path and proposed an independent raw-CU8-to-12-kHz USB analysis sidecar. Existing AM/SSB/speaker audio is not reused as the decoder signal.
- Added a tap-off bit-identity requirement and on-device CPU/drop A/B gates. No existing DSP, demodulation, filter, sound, dashboard, Hunter, OrcDial, or main.cpp glue was changed.
- Froze further Phase 1 implementation until the owner reviews the expanded research and proposals.


## 2026-10-07 — Phase 1 slice 4: ModeProfile foundation

- Owner approved moving forward with the expanded FT8/FT4/JS8 decoder architecture.
- Added pure C++ `orcsdr::ftx::ModeProfile` definitions for FT8, FT4, and descriptive JS8 submodes.
- Marked FT8 and FT4 implementation-ready; JS8 profiles remain research-pending/experimental until missing clean-room sync/FEC definitions are independently established.
- Added exact FT8 and FT4 timing, tone, frame, Costas, FEC-family and payload-transform metadata.
- Added optimized + ASan/UBSan host tests and CI coverage.
- No UI-owned files, existing DSP/demod/audio path, main.cpp glue, transmit path, or firmware binding were changed.


## 2026-10-07 — Phase 1 slice 5: shared sync candidate scorer

- Added a pure C++ ModeProfile-driven synchronization scorer over an abstract spectral-energy grid.
- Added configurable time/frequency oversampling geometry so the search is not tied to one FFT layout.
- Added local competing-tone normalization, bounded candidate ranking, and non-maximum suppression without heap allocation.
- The sync score is documented as a dimensionless contrast metric, not SNR.
- FT8 and FT4 synthetic sync searches pass optimized + ASan/UBSan tests; JS8 research-pending profiles are explicitly refused.
- No FFT/channelizer, firmware binding, UI-owned file, existing audio path, or transmit code was changed.


## 2026-10-07 — Phase 1 slice 6: shared soft demodulation

- Extended ModeProfile with protocol-defined data blocks and tone-to-bit Gray labels.
- Added pure C++ candidate-local soft demodulation shared by FT8 and FT4.
- The demodulator emits exactly 174 dimensionless max-log-style LLRs with the same sign convention as the NMS decoder.
- Deterministic FT8 and FT4 tests verify all 174 soft-bit signs; equal-tone input produces zero reliability.
- JS8 remains disabled/research-pending.
- No FFT/channelizer, firmware binding, UI-owned file, existing audio path, or transmit code was changed.


## 2026-10-07 — Phase 1 slice 7: incremental spectral reference

- Added a pure C++ streaming 12 kHz exact-correlation spectral reference backend with caller-owned output and fixed workspace.
- Added irregular-chunk streaming tests and exact-tone peak validation.
- Added the first end-to-end synthetic FT8 coded-frame recovery test: PCM -> spectral grid -> Costas sync -> soft demod -> NMS LDPC -> CRC.
- The recovered 91-bit message and 174-bit codeword must exactly match the injected test fixture.
- This is not yet a user-visible FT8 message decode; message unpack/plausibility remains mandatory.
- Documented future conducted RF verification using an external Pluto-compatible signal source over coax/attenuation while OrcSDR itself remains RX-only.
- No firmware audio tap, UI-owned file, existing receive DSP, PTT, CAT, or transmit path was added.


## 2026-10-07 — external receive-validation plan

- Documented official WSJT-X FT8/FT4 receive WAVs as external benchmark inputs.
- Defined a three-layer owner-controlled corpus: raw RTL CU8 IQ, 12 kHz analysis PCM, and decoder result logs.
- Defined conducted Pluto-compatible RF replay over coax/attenuation as the preferred full-chain hardware test while OrcSDR remains strictly RX-only.
- Third-party WAVs remain external unless redistribution rights are explicit; committed real recordings should be owner-controlled.


## 2026-10-07 — Phase 1 slice 8: CRC-gated receive pipeline

- Added a heap-free internal receive pipeline connecting sync search, soft demodulation, NMS LDPC and CRC-14.
- Added a negative test proving a valid LDPC codeword with invalid CRC is not returned.
- Kept UI Decode output blocked until source-message unpack and plausibility validation exist.
- FT4 remains intentionally blocked at the final frame gate until payload XOR restoration is implemented and tested.
- No firmware binding, existing audio/DSP change, PTT, CAT or transmit functionality was added.


## 2026-10-07 — Phase 1 slice 9: standard message unpacking

- Added conservative QEX Type 1/2 source-message unpacking.
- Added inverse c28 decoding with canonical round-trip validation.
- Added g15 grid/report/acknowledgement decoding and standard text rendering.
- Recognized but did not invent text for unresolved 22-bit hashes.
- Unsupported CQ modifiers and other i3 message families remain rejected until independently implemented.
- Added optimized + sanitizer host-test coverage through the FT8 test runner.


## 2026-10-07 — Phase 1 slice 10: first full synthetic FT8 receive decode

- Added source-message plausibility to the pipeline after LDPC and CRC.
- Only fully renderable supported standard messages leave the current pipeline.
- Added a CRC-valid but unsupported-i3 negative test.
- Replaced the synthetic random-payload PCM test with a complete standard FT8 message fixture: `CQ K1ABC FN42`.
- The end-to-end host test now spans PCM -> spectral analysis -> sync -> soft demod -> NMS LDPC -> CRC -> standard message text.
- This remains host-side synthetic receive validation; it is not Tab5/RF hardware verification and adds no transmit capability.


## 2026-10-07 — Slice 10 focused review correction

- Fixed one missing closing brace in the rewritten full-decode spectral test fixture; decoder production modules were unaffected.
- Ran a focused optimized + ASan/UBSan sandbox test of the new pipeline plausibility gate.
- Verified `CQ K1ABC FN42` is accepted/rendered and an unsupported CRC-valid i3 family is rejected.
- Kept the distinction between focused sandbox validation and a full newest-head branch regression run explicit.


## 2026-10-07 — host real-WAV benchmark runner

- Added `tools/ft8-wav-benchmark.cpp` and `tools/benchmark-ft8-wav.sh`.
- The host runner accepts 12 kHz mono i16 PCM WAVs and drives the actual native receive pipeline through plausibility-gated text.
- Search resolution is configurable without changing decoder code.
- Output keeps sync/contrast metrics dimensionless; no uncalibrated SNR claim is made.
- The official WSJT-X busy-band WAV remains an external benchmark target; no decode result is claimed until the exact file is successfully run.


## 2026-10-07 — full decoder host checkpoint validation

- Temporarily enabled the decoder branch in the native-core workflow push trigger to force validation of the newest GitHub-App commit chain.
- GitHub Actions run 37683738040 passed `bash tools/test-ft8.sh` at commit `8d03508c`.
- Confirmed the complete ModeProfile/spectral/sync/demod/pipeline/message/codec/LDPC/NMS + UI/Hunter/OrcDial optimized and ASan/UBSan suite passes.
- Removed the temporary branch push trigger immediately after validation; normal push scope returns to main.


## 2026-10-07 — rebase onto multi-mode UI seam and real-WAV baseline

- Rebased the native decoder workstream onto `claude/ft8-ui` commit `be87fe47edbfe4e06d4c6794ca7c13a1bc7498fe`.
- Preserved the UI-owned DigitalMode/Decode/backend seam, dashboard/model/Hunter, OrcDial, and main.cpp integration unchanged.
- Merged `tools/test-ft8.sh` so the UI backend seam tests and all native decoder tests run together.
- Recorded the official WSJT-X `210703_133430.wav` baseline using the installed `jt9` executable as the external reference: WSJT-X reports 14 messages while OrcSDR accepts 1 at the default 80 ms / 6.25 Hz search, with zero accepted messages outside the reference list.
- Added a CI regression floor requiring at least one decode and the known `WM3PEN EA6VQ -09` message before any coverage optimization is accepted.


## 2026-10-07 — FT4 receive payload restoration gate

- Added the FT4 receive-side 77-bit XOR restoration using the pseudo-random sequence printed directly in the QEX protocol paper.
- The transform is applied only after LDPC convergence and CRC-14 validation, before source-message unpacking.
- Enabled FT4 in the shared CRC/plausibility pipeline for supported standard-message families.
- Added an end-to-end synthetic FT4 energy-grid test proving sync -> soft demod -> LDPC -> CRC -> un-XOR -> standard message rendering for `CQ K1ABC FN42`.
- No transmitter, waveform output, PTT, CAT, or firmware binding was added.


## 2026-10-07 — mode-selectable real-WAV benchmark

- Extended the host WAV benchmark to select FT8 or FT4 while using the same shared spectral/sync/demod/pipeline implementation.
- Extended external CI to download the official WSJT-X FT4 tutorial sample, run `jt9 --ft4 -d 3` as the external reference, and run OrcSDR in FT4 mode on the same recording.
- No FT4 regression floor is set until the first real measurement establishes an honest baseline.


## 2026-10-07 — FT4 external sample correction

- The older user-guide FT4 tutorial filename `200514_182053.wav` currently returns HTTP 404 from the public SourceForge sample path.
- Switched the reproducible external benchmark to currently published official WSJT Project FT4 sample `190106_000115.wav`.
- Both WSJT-X `jt9 --ft4 -d 3` and OrcSDR consume the same downloaded WAV; no third-party sample bytes are committed to the repository.


## 2026-10-07 — frozen real-recording baselines

- Measured the current installed WSJT-X `jt9` executable against the exact same external WAV bytes used by OrcSDR.
- FT8 `210703_133430.wav`: WSJT-X 14 versus OrcSDR 1, 7.1% coverage, zero observed false accepts, 4.05 s regression-floor host wall time (5.15-5.94 s on earlier equivalent hosted runs).
- FT4 `000000_000002.wav`: WSJT-X 19 versus OrcSDR 2, 10.5% coverage, zero observed false accepts, 1.90 s regression-floor host wall time (2.29 s on the prior survey).
- Added explicit FT8 and FT4 external-WAV CI floors for the currently stable accepted messages.
- OrcSDR SNR remains unimplemented and is not fabricated in benchmark output.

## 2026-10-07 — Task 3 experiment 1: search-geometry measurement and miss classification

- Instrumented the receive chain without changing what it accepts: `pipeline::try_candidate` (with `Outcome` and `CandidateTrace`) is now the single
  per-candidate path, and `decode_grid` calls it, so production and diagnostics cannot diverge.
- Added the host tools `tools/ft8-wav-diagnose.cpp` / `tools/diagnose-ft8-wav.sh` and the WSJT-X reference lists in `tools/ft8-reference/`. Every reference
  signal is classified (decoded, no sync candidate, suppressed by NMS, ranked beyond the limit, demod/LDPC/CRC failure, unsupported message, out of analysis
  band, starts before the recording, truncated by the end), with per-stage candidate and LDPC/CRC counts and timings. The shared WAV reader moved to `tools/ft8_wav_common.hpp`.
- Measured FT8 and FT4 at 2,1 / 4,1 / 4,2 on the official recordings: FT8 1 to 2 to 5 accepted (6 with the limit lifted), FT4 2 to 2 to 3, zero false accepts, and zero accepts on 48
  deterministic noise runs. Results and diagnosis are in `REAL_WAV_BENCHMARK.md`.
- Found and documented (not yet fixed): the standard-callsign encoder rejects callsigns with more than one digit (`A92EE`), and FT4 contest message types are unparsed.
- No decoder math, regression floor, firmware, UI or audio path changed.

## 2026-10-07 — Task 3 experiment 2: candidate-local time/frequency refinement (host-only measurement)

- Added `tools/ft8-wav-refine.cpp` (host only, exact correlation): coarse 2,1 search, per-candidate sample/sub-bin refinement scored on sync symbols, then the unchanged `pipeline::try_candidate`. Includes a `--no-refine` control.
- Measured: FT8 official recording 1 to 6 accepted (K=64) versus the unrefined control, equal to the 4,2 grid at about a quarter of its spectral cost; FT4 2 to 3. Zero false accepts on both recordings and on 16 noise recordings.
- No production decoder code, acceptance rule, firmware, UI or audio path changed. Not an ESP32-P4 timing.

## 2026-10-07 — Task 3 experiment 3: candidate capacity and refined re-ranking (host-only)

- `ft8-wav-refine` gained `--gate N`: refine K candidates, rank by refined sync score, attempt the best N. FT8 reaches 6 and FT4 3 with K=64 and only 16 gate attempts; larger K gives no further gain on the official recordings. Zero false accepts, zero accepts on 16 noise recordings. No production code changed.
