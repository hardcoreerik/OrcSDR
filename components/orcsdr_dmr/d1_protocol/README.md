# OrcSDR DMR D1 — fixed-memory Tier II burst protocol foundation

**Host-only.** This directory is an independent, C++17, heap-free, hard-dibit
DMR Tier II burst decoder. It is **not** a 4FSK demodulator, audio/vocoder,
firmware component, trunk-following receiver, or production-ready decoder.
No files outside `components/orcsdr_dmr/d1_protocol/**` need to be edited.

## Inputs and verified output

`orcsdr::dmr::d1::Decoder::feed()` takes contiguous hard-sliced dibits in
**transmitted order**, values 0–3 (high bit first). It accepts arbitrary chunk
boundaries, never retains caller pointers, and returns false *without changing
state* if any input dibit is invalid. The synchronous `noexcept` callback
receives an `Event` with an evidence level and separate validation flags.
`reset()` clears the entire bounded state. One event is emitted for each
matching 48-bit voice/data sync. Failed integrity checks never promote radio
IDs. Caller callbacks are responsible for any logging or buffering.

The decoder scans a 132-dibit/264-bit data/voice burst, split as 98-bit
payload + 10-bit slot type + 48-bit SYNC + 10-bit slot type + 98-bit payload.
A base-station CACH is 24 bits (12 dibits) preceding its 132-dibit burst;
D0's CACH mapping/Hamming correction supplies **inferred** BS slot numbers.
Direct TS1/TS2 sync provides slot attribution without CACH. Other BS/MS
bursts with no usable preceding CACH keep `slot=0` (unknown). Because CACH
TACT has only Hamming protection, a CACH-derived slot is **not CRC-confirmed**.
`slot_from_cach` and `cach_tact_corrections` expose this distinction.

### Evidence and packet types

| Evidence enum | Verified condition | Identity available? |
|---|---|---|
| `sync_only` | 48-bit sync match | **No** |
| `slot_type` | Golay(20,8) unique nearest codeword at distance 0–3 | **No** |
| `fec_valid` | BPTC(196,96) row and column checks pass | **No** |
| `check_valid` | RS(12,9) or masked CRC-16 passes | **No, unless LC is recognized** |
| `identity_valid` | Full-LC/embedded-LC integrity and supported group/private FLCO | **Yes** |

Slot type is the decoded raw ETSI four-bit `data_type` code (Table 9.22):
0 PI header; 1 Voice LC Header; 2 Terminator with LC; 3 CSBK; 4/5 MBC
header/continuation; 6 Data Header; 7 rate-1/2 data; 8 rate-3/4 data;
9 idle; 10 rate-1 data; 11 Unified Single Block Data; 12–15 reserved.
The `data_type` field exists for every Golay-valid data SYNC even if the
payload type is not yet implemented.

- **Types 1/2:** D0 BPTC deinterleaving + D1 Hamming-product FEC and RS
  (12,9) integrity with the ETSI 0x96 / 0x99 parity masks. Recognizes only
  *standard* `FID=0`, FLCO=0 (group voice) and FLCO=3 (unit-to-unit voice),
  extracting 24-bit destination/talkgroup and source radio ID, protected flag,
  service options, and FLCO. The exposed `parse_identity()` requires an
  **already verified** `event.integrity_valid=true` gate and will refuse
  unchecked bytes.
- **Types 3/6/11:** BPTC and masked CRC-16 (`0xA5A5`, `0xCCCC`, `0x3333`)
  checks. CSBK and data payload field decoding are deliberately **not**
  implemented; validated CRC does **not** imply voice identity.
- **Voice late entry:** An unambiguous BS voice sync/CACH anchor followed by
  the four matching alternating-slot embedded-LC fragments B–E may produce a
  validated group/private identity. It validates QR EMB, LCSS order, colour,
  the BPTC(128,72) parity checks, and checksum before publishing an ID. It
  does not guess LC from voice sync alone and currently rejects rather than
  repairing embedded-LC parity errors. Missing fragments or irregular slot
  cadence do not produce metadata.

All buffers are `std::array` / stack and fixed-capacity: 256 dibits of history,
three 128-bit embedded-LC fragment states, and local coding matrices. No
`std::vector`, allocator, I/O, RTOS, `malloc`/`new`, or exception-based
control flow exists in the decoder. The **test harness** uses `std::vector`
freely; that is not linked into the receiver library.

## ETSI basis and independent test vectors

This implementation was authored from the normative coding descriptions
rather than copied from another DMR decoder:

- [ETSI TS 102 361-1 V2.7.1 (2026-05)](https://www.etsi.org/deliver/etsi_ts/102300_102399/10236101/02.07.01_60/ts_10236101v020701p.pdf): §6.2 burst framing, §9.1 sync/slot/EMB, Annex B.1 BPTC, B.3.5 Golay/Hamming/QR, B.3.6 RS, B.3.8 CRC.
- [ETSI TS 102 361-2](https://www.etsi.org/deliver/etsi_ts/102300_102399/10236102/): Full Link Control FLCO and group/private ID layout.
- Golay generator follows the printed matrix in
  [ETSI TS 102 361-1 V2.6.1](https://www.etsi.org/deliver/etsi_ts/102300_102399/10236101/02.06.01_60/ts_10236101v020601p.pdf), independently checked to have minimum code distance **8** across all 256 codewords. The machine-extracted V2.7.1 Table B.11 differs in two rows; those extracted values do **not** give distance 8. This implementation uses the distance-8 matrix and pins the derived `(colour=2,type=1) -> 0x21E26` codeword. This ambiguity needs checking against a published on-air slot-type reference capture before production signoff.

The tests independently construct full bursts using systematic generator
matrices, parity matrices, and CRC recurrences. Tested: all 256 Golay
codewords and their mutual distance; a pinned Golay vector; all 20 single,
190 double, and 1,140 triple bit flips on the pinned codeword; quadruple-bit
rejection; BPTC bit placement/correction; RS and CRC positive/negative
checks; group/private field IDs; 2-slot CACH and direct-mode slot detection;
stream chunks (1, 2, 3, 7, 13, 131, 144 dibits); zero false IDs from voice
sync, invalid input, bad RS, or corrupt embedded LC; valid and corrupted
four-fragment late entry. These are **synthetic host tests**, not proof of RF
lock, on-air decoder parity, or audio intelligibility.

## How to build and run

From the **OrcSDR repository root** on Linux/macOS or Windows with a C++17
compiler and CMake 3.16+:

```sh
cmake -S components/orcsdr_dmr/d1_protocol -B build/dmr-d1 -DCMAKE_BUILD_TYPE=Debug
cmake --build build/dmr-d1 --parallel
ctest --test-dir build/dmr-d1 --output-on-failure --verbose
```

`CMakeLists.txt` directly includes the existing `../src/d0.cpp` and
`../include/orcsdr_dmr/d0.hpp` from the working tree. It does **not** alter
D0's parent CMake or register an ESP-IDF component.

On GCC/Clang, run memory/undefined-behaviour sanitizers:

```sh
cmake -S components/orcsdr_dmr/d1_protocol -B build/dmr-d1-asan \
  -DCMAKE_BUILD_TYPE=Debug -DORCSDR_D1_SANITIZERS=ON
cmake --build build/dmr-d1-asan --parallel
ctest --test-dir build/dmr-d1-asan --output-on-failure --verbose
```

Both builds compile with warnings-as-errors. For a permanent log next to the
working test invocation, redirect console output to a chosen file (e.g.,
`ctest --test-dir build/dmr-d1 --output-on-failure --verbose > dmr-d1-test.log 2>&1`).
No raw IQ, captured traffic, or binary test fixture is committed by D1.

## Known gaps / acceptance boundary

1. The hard-dibit caller must perform IQ tuning, frequency/timing recovery,
   symbol slicing, inversion handling and BER tracking. No integrated receiver
   proves this module works with real captures from private REA-Results.
2. Strictly limited FID/FLCO parsing. CSBK channel grants, call assignments,
   MBC, PI Header, data headers, USBD body, proprietary FIDs, and RAS are not
   semantically decoded. This module does not perform audio/vocoder decode.
3. No burst loss concealment, rate-data payload handling, RF slot timing
   estimation, trunk-following state machine or proof of multi-frequency dual
   slot acquisition. Slot association via BS CACH is an inference and cannot
   establish system identity by itself.
4. BPTC Hamming repair is conservative; embedded late-entry parity is strict,
   without soft-decision/FEC error recovery. Strong interference, colliding
   sync, nonstandard channel inversion, and truncated multi-burst voice
   sessions require additional independently observed tests.
5. Header/terminator RS and masked CRC validation establish **packet**
   integrity, not validity of directory aliases, site naming, voice privacy,
   encryption handling, or higher-layer sequence/talkgroup context.
6. No firmware integration, hardware profiling, storage/protocol migrations,
   or changes to D0/shared files. Those require separate phases.
