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
