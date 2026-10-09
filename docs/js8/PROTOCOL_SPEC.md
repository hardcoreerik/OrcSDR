# JS8 receive protocol specification — verification gate

Status: **INCOMPLETE / NOT APPROVED FOR DECODER IMPLEMENTATION**  
Scope: independent OrcSDR receive-only specification. No decoder source is included.

## Verification blocker (2026-10-08)

The only real-signal reconstruction evidence accessible during this session was `docs/js8/results/2026-10-08-sample-40m-180s-002-front-end.json` on branch `claude/ft8-native-bind`. The PC corpus directory `F:\\AI\\OrcSDR-TEMP\\js8-pc-capture` and the installed reference CLI/GUI are **not accessible** in this environment. Direct cloning of the external source repository also failed because the sandbox cannot resolve github.com. Accordingly, no recorded run establishes parity-check satisfaction, CRC success, FEC correction count, or recovered message text. **Do not treat the following source-derived facts as verified against this corpus.**

Dataset identifiers provided by owner (not locally rehashed):
- dataset commit `1705472`
- WAV SHA256 `93b48bef6a6799e0301d1459ee724759772f5e0ff8e83249337a82747c44a445`
- IQ SHA256 `6eff2c58b939422023fa69c0a7e2b3180f662767dacf451136e72aa834aca83f`

## Evidence / provenance ledger

| Fact | Value | Provenance | Class | Real-capture verification |
|---|---|---|---|---|
| Channel-symbol count | 79 | JS8Call User Guide, Technical Implementation / Modulation, https://js8call.com/JS8Call-improved/d6/d14/md_docs_2user__guide_2JS8Call__User__Guide.html | DOC | NO |
| Tones | 8 | Same | DOC | Saved tone records each contain 79 indices 0..7; this does not prove an entire valid frame |
| Sync positions | 0–6; 36–42; 72–78 (0-indexed) | JS8Call public `JS8.h` Costas documentation and independent geometry; https://js8call.com/JS8Call-improved/d7/dd8/JS8_8h.html | GPL-SOURCE (published generated source reference) | Partial: candidate tone records display repeated patterns; some are corrupted |
| Normal sync groups | [4,2,5,6,1,3,0] repeated three times | `JS8::Costas::array`, `JS8.h`, published generated documentation (exact source commit/line range PENDING) | GPL-SOURCE | Partial |
| Fast/JS8 40/Slow sync groups | [0,6,2,3,5,4,1], [1,5,0,2,3,6,4], [2,5,0,6,4,1,3] | Same, modified Costas variant; exact tagged version and line range PENDING | GPL-SOURCE | UNVERIFIED |
| Data-tone positions | 7–35 and 43–71, 58 × 3 = 174 channel bits | Derived from 79 symbols and 21 sync symbols; source documentation as above | DERIVED | Physical extraction only |
| FEC dimensions | LDPC(174,87), 87 information + 87 redundancy bits | `JS8.cpp` constants `N`, `K`, `KK`; https://github.com/js8call/js8call/blob/main/JS8.cpp ; exact commit and lines PENDING | GPL-SOURCE | UNVERIFIED |
| Pre-CRC message size | 75 bits = 72 payload + 3 header | JS8Call documentation and `JS8.cpp` `KK` description; actual bit ordering PENDING | DOC / GPL-SOURCE | UNVERIFIED |
| CRC width | 12 bits | `JS8.cpp`, `CRC12` and `checkCRC12`; exact commit and lines PENDING | GPL-SOURCE | UNVERIFIED |
| CRC polynomial representation | 0xC06 in Boost augmented-CRC representation, with documented output XOR 42 | `JS8.cpp` `CRC12`; exact initialization, augmentation semantics, bit coverage and byte padding must be documented before adoption | GPL-SOURCE | UNVERIFIED |
| Normal symbol duration and spacing | 0.160 s, 6.25 Hz; 12.64 s for 79 symbols | JS8Call User Guide modulation table | DOC | UNVERIFIED |
| Fast | 0.100 s symbol, 10 Hz, 7.9 s burst | Same | DOC | UNVERIFIED |
| JS8 40 (formerly Turbo) | 0.050 s symbol, 20 Hz, 3.95 s burst | Same | DOC | UNVERIFIED |
| Slow | 0.320 s symbol, 3.125 Hz, 25.28 s burst | Same | DOC | UNVERIFIED |
| JS8 60 | Experimental; stable public technical specification not established | Same | DOC | UNVERIFIED |
| Minimum sync constant in one decoder | `ASYNCMIN=1.5`; NOT an OrcSDR threshold recommendation | `JS8.cpp` `ASYNCMIN`, main branch, exact revision PENDING | GPL-SOURCE | UNVERIFIED |

## Source-version and verification caution

Public source/documentation confirms JS8 historically uses **LDPC(174,87) / CRC-12**, rather than modern FT8's LDPC(174,91) / CRC-14. It does **not** establish that the existing OrcSDR pre-FEC tone decisions produce a valid codeword under the source's parity matrix. The source's real 174-bit parity-check/generator matrix, column ordering, exact CRC byte/bit coverage, tone mapping, message type packing and message assembly are **not yet frozen or verified** here. The GPL source repository version/commit must be pinned before those tables are published as implementation inputs.

## Raw candidate caution

The saved front-end results contain repeated captures of several frequencies with varying 79-tone strings. Some entries begin with non-Costas tones, while others contain all 21 expected Normal sync tones. These are **candidate estimates**, not established error-free codewords. Hold out K7YXZ (838 Hz) for verification, and choose candidates by an independent quality criterion, not by whether a fitted parity/CRC model accepts them.

## Blocked acceptance test

Required proof for each reference frame: precise source WAV metadata and hash -> chosen 79 tones -> 58 data symbols -> 174 bits -> parity syndrome before/after corrections -> CRC-12 -> 75 bits -> complete text. Record both the exact commands and exit status. Repeat on the held-out 838 Hz frame. Without these artifacts, no claim of end-to-end correctness or FEC-corrected bits is permissible.

## Open questions

1. Can the owner mount or attach the actual corpus (including `audio.wav`, `index.json`, and per-capture `fixture.json`) to this environment, or run the host verification there?
2. Which pinned commits correspond to the installed JS8Call CLI 2.2.0 and GUI 3.0.3? Did either modify decoder semantics?
3. What is the exact generator/parity-check and column-order representation for that version, and does it validate held-out channel bits?
4. What are the canonical CRC input padding, reflect, seed, and transmitted bit ordering for the chosen version?
5. Can any independently validated frame demonstrate heartbeat, callsign, SNR field and multi-frame assembly bit layouts?
6. Are different JS8 60 variants relevant to the releases OrcSDR intends to support?

**STOP GATE:** Do not implement a decoder from this provisional ledger until the parity, CRC and message-layer evidence has been produced.

## 2026-10-08 supplemental kit audit — supersedes the earlier data-access blocker

The owner subsequently provided three zip archives: core dataset, primary IQ, and packaged reference CLI. In a host sandbox, the primary WAV, IQ and CLI executable SHA-256 **all matched** their independently supplied expected hashes. Thus the earlier statement that the corpus is inaccessible is outdated: the corpus is now available in the sandbox. No claim of FEC, CRC, or rendered text verification is justified yet.

A host-only `tools/js8-spec/verify_spec_evidence.py` run against the extracted core kit recorded 16 candidate 79-tone strings, 58 extracted data tones per candidate, and these best observed Normal Costas hit counts: 635/637.5 Hz = **21/21**, 486/487.5 Hz = **21/21**, held-out 838/837.5 Hz = **14/21**. The fourth 2604 Hz reference message lacks a candidate tone sequence. This is a **physical-stage audit**, not a proof of error-corrected channel symbols. The saved machine-readable result is `docs/js8/spec/evidence-2026-10-08.json`.

The CLI executable's hash matched the packaged Ubuntu 2.2.0 reference, but execution failed before decoding: `libicui18n.so.74: cannot open shared object file`. It would be incorrect to report a new black-box decode run. Existing fixture reference records remain evidence collected previously, not rerun here.

Public GitHub connector access to the GPL source repository is now available for source examination; direct `git ls-remote https://github.com/js8call/js8call.git HEAD` in the host sandbox still fails DNS resolution. Current `main` source `JS8.cpp` was retrieved through the connector, but its blob SHA alone is not an exact pinned commit and current source may differ from JS8Call 2.2.0. Version-specific provenance, source line ranges, and tested implementation constants remain unresolved.

**Outstanding gate:** obtain a version-pinned matrix/tone map/CRC specification and execute full 174-bit syndrome + CRC + decoded-payload checks on the actual tone vectors. The held-out 838 Hz frame cannot be labeled verified merely because it contains 14 matching sync symbols.
