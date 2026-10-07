# OrcSDR Native FT8 Decoder — Phase 1 Implementation Notebook

Status: **IN PROGRESS**

Branch: `codex/ft8-native-decoder-research`

Owner approval to proceed from Phase 0 was received on 2026-10-07. This notebook records implementation decisions, provenance, validation, and measurements as the native decoder is built. It does not claim hardwar verification unless a named Tab5 run is recorded.

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

Run on Linux/WSL:

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

## Next slice

The next isolated slice is the FT8 LDPC definition and correctness layer:

1. import the public-domain `generator.dat` and `parity.dat` values as provenance-tracked protocol constants (not decoder code).
2. implement systematic 91->174 encoding;
3. implement parity/syndrome checking;
4. generate fixed vectors and single/multi-bit corruption tests;
5. cross-check the native encoder/checker against the public matrices;
6. only after that, implement the independently designed normalized-min-sum decoder.
