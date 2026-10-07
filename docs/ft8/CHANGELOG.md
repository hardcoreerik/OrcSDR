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
