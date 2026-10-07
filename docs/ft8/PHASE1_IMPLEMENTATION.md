# OrcSDR Native FT8 Decoder — Phase 1 Implementation Notebook

Status: **IN PROGRESS — expanded FT8/FT4/JS8 architecture approved**

Branch: `codex/ft8-native-decoder-research`

Integration base: `claude/ft8-ui` at `804268c71af127490364c325a76e61383042f137`

Owner approval to proceed from the original FT8-only Phase 0 was received on 2026-10-07. After the later FT4/JS8 scope expansion, the owner also approved moving forward with the ModeProfile architecture and related decoder plan. The decoder branch is based on the FT8 UI baseline and all implementation remains branch-only. This notebook does not claim hardware verification unless a named Tab5 run is recorded.

## Ground rules

- Receive-only decoder. No transmitter or PTT path.
- Pure protocol/DSP core remains host-buildable with plain C++17 and no ESP-IDF headers.
- No decoder implementation source is copied or translated from WSJT-X, ft8_lib, ft8mon, mfsk-core, or another FT8 decoder.
- Normative FT8 constants and algorithms may come from the Franke/Somerville/Taylor QEX protocol paper and its explicitly public-domain reference [14] resources.
- Generic FEC/DSP algorithms are independently implemented from general literature.
- Existing decoders are external comparison targets only.
- No allocation is permitted in the future per-slot firmware hot path.

## Slice 1 — protocol primitives

Files:

- `apps/orcsdr-tab5/ui/ft8_codec.hpp`
- `apps/orcsdr-tab5/ui/ft8_codec.cpp`
- `tests/ft8_codec_tests.cpp`
- `tools/test-ft8.sh`
- `tools/test-ft8.ps1`

Implemented in this slice:

- FT8 protocol size constants: 77 source bits, CRC-14, 91-bit message word, 174-bit codeword, 58 data symbols, 79 channel symbols.
- CRC-14 calculation and verification.
- Standard callsign-to-c28 primitive for one/two-character prefixes, one digit, and one-to-three-letter suffixes.
- 10-, 12-, and 22-bit callsign hashing.
- FT8 three-bit Gray mapping to tone index.
- Three Costas synchronization blocks and 58-data-symbol framing into 79 channel tones.

Not implemented yet:

- LDPC generator/parity tables or decoder.
- Full 77-bit message-type pack/unpack.
- Maidenhead/report packing.
- Audio generation.
- Sync search, demodulation, FEC decoding, subtraction, AP, or firmware integration.

### Normative provenance

The CRC polynomial/zero-extension behavior, callsign field definitions, callsign hash algorithm, Gray map, and Costas sequence are defined by the QEX paper and the authors' public-domain `ft4_ft8_protocols.tgz` resources. For this slice the public-domain helper files used as independent reference oracles were:

- `gen_crc14.f90`
- `hashcodes.f90`
- `std_call_to_c28.f90`

They were read only as protocol-definition resources explicitly placed in the public domain by Section 9 of the QEX paper. No code from an FT8 decoder implementation was used.

### Independent cross-check

Before committing this slice, the sandbox compiled the public-domain helper programs as external test oracles and compared the independently written C++ primitives against them:

- 100 deterministic random 77-bit payloads: CRC matched 100/100.
- 100 deterministic random legal hash strings: h10/h12/h22 matched 100/100.
- 100 deterministic standard calls with full three-letter suffixes: c28 matched 100/100.

Total: **300/300 oracle comparisons passed**.

The committed regression suite also contains fixed public-domain-derived vectors, including the `gen_crc14` example whose CRC is binary `01010101111001` (decimal 5497).

### Host validation

Run on Linux/WSC:

```bash
bash tools/test-ft8.sh
```

Run from the normal Windows development environment:

```powershell
.\tools\test-ft8.ps1
```

The script builds and runs both:

1. optimized `-O2 -Wall -Wextra -Werror -pedantic` tests;
2. AddressSanitizer + UndefinedBehaviorSanitizer tests.

### Design notes

The public `std_call_to_c28` helper illustrates the mixed-radix field encoding, but OrcSDR does not use the helper's command-line string alignment as its validity rule. The native implementation validates the protocol definition directly: a one-or two-character prefix with at least one letter, exactly one decimal digit in the prefix/digit position, then one to three suffix letters. It constructs the six protocol positions explicitly and only then applies the public mixed-radix mapping.

The test-side encoder remains protocol infrastructure only. Nothing in this slice is wired into the Tab5 firmware build or any RF/transmit path.

## Slice 2 — LDPC definition and correctness

Implemented after Slice 1 passed both FT8-core CI and Documentation Truth:

- Added `ft8_ldpc.hpp/.cpp` with a systematic 91-to-174 encoder and sparse syndrome checker.
- Packed the public-domain 83x91 generator matrix into 996 bytes of protocol constants.
- Stored the public-domain sparse parity-check graph as 174 columns x 3 zero-based check indices (522 bytes).
- Verified all 91 message basis vectors encode to zero-syndrome codewords.
- Verified all 174 single-bit corruptions produce nonzero syndrome with exactly three failed checks, matching the published column weight.
- Verified 1,000 deterministic pseudo-random 91-bit messages encode to zero-syndrome codewords.
- Added a fixed matrix-derived parity vector based on the public CRC example.
- Host optimized + ASan/UBSan tests pass.

No soft decoder exists yet; this slice proves only the code definition/encoding/parity layer.

### Slice 2 provenance

`generator.dat` and `parity.dat` are protocol resources from the same QEX authors' explicitly public-domain `ft4_ft8_protocols.tgz` bundle. The repository stores a compact generated representation, not code from an existing decoder. Generator-based encoding is cross-checked against the independently supplied parity-check matrix in every regression run.

## Next slice

The next isolated slice is the independently designed normalized-min-sum soft decoder:

1. build static edge/check adjacency from the public parity graph;
2. define LLR sign/scale conventions with deterministic BPSK-style LDPC channel tests;
3. implement normalized min-sum with fixed iteration cap and early syndrome termination;
4. report iterations and unsatisfied checks without dynamic allocation;
5. sweep deterministic AWGN at the codeword level before FT8 tone demodulation exists;
6. only then connect soft FEC to FT8 demodulation.

The completed LDPC correctness slice followed the original plan:

1. import the public-domain `generator.dat` and `parity.dat` values as provenance-tracked protocol constants (not decoder code).
2. implement systematic 91->174 encoding;
3. implement parity/syndrome checking;
4. generate fixed vectors and single/multi-bit corruption tests;
5. cross-check the native encoder/checker against the public matrices;
6. only after that, implement the independently designed normalized-min-sum decoder.


## Slice 3 — normalized-min-sum LDPC soft decoder

Implemented after Slice 2 passed both FT8-core CI and Documentation Truth.

- Added `ft8_ldpc_decode.hpp/.cpp`: independently written normalized-min-sum Tanner-graph decoder.
- LLR convention: positive favors bit 0; negative favors bit 1.
- Moved the one public-domain FT8 sparse parity graph into `ft8_ldpc_graph.hpp`; both syndrome checking and soft decoding consume that same definition. Decoder check adjacency is derived at compile time rather than duplicated.
- No heap allocation occurs in `decode()`. Caller-owned `Workspace` is 4,872 bytes on the host build and tests enforce a <=5,000-byte bound.
- Already-valid hard decisions exit at iteration 0.
- Every NMS iteration checks the syndrome and exits immediately on parity convergence.
- A wider loop counter keeps the legal 255-iteration configuration bounded instead of wrapping.
- NaN/non-finite LLRs and invalid normalization/LLR-limit configuration are rejected.

### Truth boundary

`Result::converged` means **LDPC parity convergence only**. It is not an accepted FT8 decode. A noisy observation can converge to a different valid LDPC codeword. The later pipeline must pass CRC-14 and legal message unpack/plausibility checks before producing an `orcsdr::ft8::Decode`.

### Provisional normalization measurement

A deterministic host BPSK/AWGN benchmark uses the same 2,000 codewords/noise samples for every normalization factor at a given sigma:

| sigma | alpha | converged / 2000 | wrong valid codeword | avg iterations |
|---:|---:|---:|---:|---:|
| 0.75 | 0.70 | 1680 | 1 | 7.66 |
| 0.75 | 0.75 | 1719 | 2 | 7.38 |
| 0.75 | **0.80** | **1731** | 2 | **7.32** |
| 0.75 | 0.85 | 1727 | 2 | 7.49 |
| 0.85 | 0.70 | 763 | 0 | 15.28 |
| 0.85 | 0.75 | 816 | 0 | 14.94 |
| 0.85 | **0.80** | **839** | 1 | **14.85** |
| 0.85 | 0.85 | 826 | 1 | 15.01 |
| 0.95 | 0.70 | 159 | 0 | 19.15 |
| 0.95 | 0.75 | 189 | 0 | 19.02 |
| 0.95 | **0.80** | **194** | 0 | **18.98** |
| 0.95 | 0.85 | 188 | 0 | 19.03 |

This supports **0.80 as the provisional synthetic-channel baseline**. It is not yet an FT8-optimal claim; tone-derived LLR statistics may move the optimum and must be re-swept after demodulation exists.

The complete 24,000-decode sweep took 1.805 seconds on the current cloud host after the shared-graph refactor. This is a host-only measurement and says nothing about ESP32-P4 latency.

Run it with:

```bash
bash tools/benchmark-ft8-ldpc.sh
```

Optimized and ASan/UBSan regression tests cover clean iteration-0 exit, deterministic ten-hard-error recovery, bounded non-convergence, invalid inputs, a 255-iteration no-wrap case, and 100 deterministic messages with six weak wrong hard decisions each.

### Next slice

Build the 77-bit source-message pack/unpack + CRC acceptance layer before spectral/demod work. This gives the FEC pipeline the required truth gate: parity convergence alone is never user-visible.


## Expanded-scope freeze

The later product scope adds FT4 and JS8Call plus a strict requirement for an independent 12 kHz USB analysis tap that cannot alter existing audio.

The following documents now form the active review gate:

- `docs/ft8/DECODER_RESEARCH.md`, Sections 26-32;
- `docs/ft8/MODE_PROFILE_DESIGN.md`;
- `docs/ft8/DECODER_SEAM_PROPOSALS.md`;
- `docs/ft8/AUDIO_TAP_PROPOSAL.md`.

The existing FT8 codec, LDPC definition, and normalized-min-sum decoder remain valid checkpoint work. No sync/demod, FT4, JS8, audio-tap, or firmware binding is added while this gate is active.


## Slice 4 — shared ModeProfile foundation

Owner approved the expanded multi-mode direction and implementation resumed.

Added decoder-owned `ft8_mode.hpp/.cpp` in namespace `orcsdr::ftx` with data-driven profiles for:

- FT8;
- FT4;
- JS8 Normal;
- JS8 Fast;
- JS8 40;
- JS8 Slow;
- JS8 60 / Ultra experimental.

FT8 and FT4 are marked `implementation_ready`. JS8 profiles are intentionally not enabled yet:

- JS8 Normal carries only independently supportable FT8-family timing/Costas facts and is `research_pending`;
- JS8 Fast/40/Slow carry verified timing/tone facts but leave modified Costas arrays unset;
- JS8 60 is explicitly `experimental`.

This prevents GPL JS8Call source constants from silently becoming native OrcSDR implementation data while keeping the architecture ready for later independent derivation/interoperability tests.

The profile captures slot time, 12 kHz symbol length, channel/data/ramp symbol counts, tone count/spacing, sync family/blocks, FEC family, payload transform, and readiness.

Host tests validate FT8 and FT4 exact frame invariants, JS8 descriptive profiles, invalid-profile rejection, optimized C++17 compilation, and ASan/UBSan.

The next decoder-owned slice is shared synchronization/candidate geometry built against `ModeProfile`. It will start with FT8/FT4 only; JS8 remains disabled until the missing clean-room protocol data is resolved.


## Slice 5 — ModeProfile-driven synchronization scorer

Added pure C++ `ft8_sync.hpp/.cpp` as the shared Costas/synchronization geometry layer.

The module intentionally consumes an **abstract non-negative energy grid** rather than FFT samples. This keeps synchronization logic independent of the eventual P4 spectral backend.

Key properties:

- supports time oversampling with `rows_per_symbol`;
- supports frequency oversampling with `bins_per_tone`;
- scores only protocol-defined sync symbols from the active `ModeProfile`;
- uses local competing-tone energy at each sync symbol to normalize the expected Costas tone;
- returns a bounded contrast score, explicitly **not SNR**;
- performs bounded heap-free candidate search and local non-maximum suppression;
- refuses research-pending/experimental profiles, so JS8 cannot accidentally become enabled through shared code.

Host tests cover uniform-energy rejection, exact FT8 candidate recovery, FT4 recovery on a 2x time/frequency oversampled grid, JS8 research-pending rejection, non-finite energy rejection, and optimized + ASan/UBSan builds.

This slice does not perform FFT/channelization and does not claim detection performance on RF. The next slice is candidate-local tone-energy/soft-bit demodulation driven by ModeProfile, initially FT8/FT4 only.


## Slice 6 — shared candidate-local soft demodulation

Extended ModeProfile with explicit data blocks and bidirectional Gray labels per tone. This keeps data-symbol extraction and soft demodulation mode-driven instead of hard-coded per protocol.

FT8 profile now carries:
- two 29-symbol data blocks at channel positions 7 and 43;
- tone labels matching QEX Table 3.

FT4 profile now carries:
- three 29-symbol data blocks at channel positions 5, 38, and 71;
- the four-tone Gray labels from QEX Table 3.

Added pure C++ `ft8_demod.hpp/.cpp`:
- reads only candidate-local non-negative tone energies;
- produces exactly 174 max-log-style soft metrics for FT8 and FT4;
- positive LLR favors bit 0, matching the existing NMS decoder convention;
- normalizes each symbol by its mean tone energy and clips to a configured finite bound;
- reports a separate dimensionless mean symbol contrast, not SNR;
- refuses JS8 while its profiles remain research-pending.

Deterministic tests synthesize all data symbols for both FT8 and FT4 and verify the sign of all 174 recovered soft bits. Equal-energy tones produce zero reliability. Optimized and ASan/UBSan builds pass.

The next major missing piece is the incremental spectral frontend that turns 12 kHz PCM into the abstract energy grid used by sync and demod. That work remains host-first; the approved raw-CU8 audio tap is a later firmware-binding layer.
