# Independent D0 / D1 DMR protocol conformance

**Scope: host only**, independent tests and findings. The original implementations are read-only. There is no production source change, SDR device involvement, firmware/UI integration, or third-party decoder/source copying.

## Standards pinned and verified against *rendered* pages

- [ETSI TS 102 361-1 V2.7.1 (May 2026)](https://www.etsi.org/deliver/etsi_ts/102300_102399/10236101/02.07.01_60/ts_10236101v020701p.pdf): PDF page indices 90 (`Table 9.2`), 91 (`Tables 9.3–9.5`), 59 (`Figures 6.4–6.5`), 121 (`Figure B.1`), 124–125 (`Figures B.2–B.3`), 136 (`Table B.11`), 138 (`Table B.18`), 140 (`B.3.8`) and 144 (`Table B.21`). The page indices are **zero-based PDF pages**, printed page number is index + 1.
- [ETSI TS 102 361-1 V2.6.1 (May 2023)](https://www.etsi.org/deliver/etsi_ts/102300_102399/10236101/02.06.01_60/ts_10236101v020601p.pdf): rendered page index 136, printed page 137, to resolve the `Golay(20,8)` issue.
- [ETSI TS 102 361-2 V2.5.1 (May 2023)](https://www.etsi.org/deliver/etsi_ts/102300_102399/10236102/02.05.01_60/ts_10236102v020501p.pdf): PDF page index 72 (printed page 73), Tables 7.1 and 7.2 for standard group/private voice Full LC identities. This is *not* an AMBE/vocoder test.

### Golay matrix ambiguity: **resolved**

The printed `Golay(20,8)` generator matrices on PDF page index **136** in **both** V2.6.1 and V2.7.1 are identical. Some machine-extracted table output differed, but a machine extraction is **not the authoritative ETSI table**. The checked matrix yields systematic 20-bit words with 12 parity bits:

```
3DA D99 6CD 367 DC6 A97 93E 8EB
```

The reference independently checks all 256 codewords, minimum Hamming distance **8**, a fixed `0x21 -> 0x21E26` example, all 1,140 triple-bit corruptions of that word, and disallowance of every contiguous four-bit corruption. **No Golay matrix defect is established in D1** from this ambiguity. This is a specification conformance result, **not** an on-air comparison.

## Run / test — complete instructions

From the **OrcSDR repository root** with Python 3.10+:

```sh
python3 -m unittest discover -s tests/dmr_protocol_conformance \
  -p 'test_etsi_oracle.py' -v
```

These **13 pure-reference standard-library tests** run without the decoder build. They establish independently calculated ETSI values and corruption properties, not execution of D0/D1 code.

For the actual **production-linked** C++17 verification, install CMake ≥3.16, a C++17 compiler (GCC/Clang/MSVC), then from the same repository root:

```sh
cmake -S tests/dmr_protocol_conformance -B /tmp/orcsdr-dmr-conformance \
  -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/orcsdr-dmr-conformance --parallel
ctest --test-dir /tmp/orcsdr-dmr-conformance --output-on-failure --verbose
```

The CMake target compiles existing source files from `components/orcsdr_dmr/src/d0.cpp` and `components/orcsdr_dmr/d1_protocol/src/d1.cpp` **read-only**. No links to third-party code or firmware components are required. Five CTest suites are registered:

| Suite | Expected checks |
|---|---|
| `dmr_conformance_golay` | Printed ETSI SYNC values; exhaustive Golay(20,8) codebook; 3-bit correction / 4-bit rejection; untouched invalid-output parameters |
| `dmr_conformance_cach_bptc` | CACH TACT mapping/repairs; every BPTC permutation bit; independent product-coded vectors and flipped payload; invalid padding |
| `dmr_conformance_integrity` | ETSI RS(12,9) matrix and 24-bit masks; CRC-CCITT with three 16-bit masks; full-LC group/private identity gating; damaged content and unrecognized FID rejection |
| `dmr_conformance_stream` | Two-slot BS bursts; every defined sync; arbitrary input chunking/reset/invalid dibits; false-CACH provenance reproduction (documented in FINDINGS.md) |
| `dmr_conformance_late_entry` | BPTC(128,72), QR(16,7), checksum and 4-stage embedded-LC identity; corruption/missing-stage rejection and chunk boundaries |

Optional sanitizers (Linux with GCC/Clang):

```sh
cmake -S tests/dmr_protocol_conformance -B /tmp/orcsdr-dmr-conformance-asan \
  -DCMAKE_BUILD_TYPE=Debug -DDMR_CONFORMANCE_SANITIZERS=ON
cmake --build /tmp/orcsdr-dmr-conformance-asan --parallel
ctest --test-dir /tmp/orcsdr-dmr-conformance-asan --output-on-failure --verbose
```

To save complete test results next to the invocation (`pwd`):

```sh
ctest --test-dir /tmp/orcsdr-dmr-conformance --output-on-failure --verbose \
  > ./dmr-conformance-host.log 2>&1
```

On Windows use `py -3 -m unittest discover ...` and let CMake pick the installed native toolchain; use a build path such as `build/dmr-conformance` rather than `/tmp`.

## Actual execution evidence (this sandbox, 2026-11-11)

| Action | Evidence | Result |
|---|---|---|
| Independent Python reference | `python3 -m unittest discover -s ... -p test_etsi_oracle.py -v` | **PASS: 13/13**, 0 failures, ~0.08 sec in this sandbox |
| Independent mathematical Golay sweep | all 256 × 255 / 2 pairs | **d_min = 8**, codeword(0x21) = 0x21E26 |
| CACH Hamming covering sweep | all 128 possible 7-bit TACT values | **max distance to a valid TACT = 1** (proven; cannot authenticate CACH provenance) |
| C++ test compilation/syntax | `g++ -std=c++17 -fsyntax-only -Wall -Wextra -Wpedantic -Werror` with signature-compatible temporary stub headers | **PASS syntax only**, NOT source-linked or executed |
| C++ production-linked CMake/CTest | Requires checked-out D0/D1 sources; no GitHub DNS/checkout available in sandbox | **NOT RUN; do not interpret syntax result as a PASS** |
| Sanitizers, hardware, recorded IQ | Not available here | **NOT RUN** |

No raw IQ or sensitive data is included. See [FINDINGS.md](FINDINGS.md) for exact repro, severity and the evidence boundary. Read-only reference test data in this folder are not a decoder implementation. Regression triage should attach a host CTest log from a full checkout before considering defects reproduction-tested end-to-end.

## Architectural gates / limitations

- The public host D1 input takes **hard dibits**, not IQ. These tests do not evaluate frequency discrimination, timing recovery, fading, 4FSK soft confidence, frame-rate sensitivity, or RF hardware.
- A valid SYNC is **not** a CRC-valid message, nor proof of P25/DMR identity. `slot_from_cach` is an inferred source field, not a transmitter location or an authenticated slot proof.
- Late entry tested is the scoped **base-station voice-sync** path; direct-mode voice late entry, vendor-specific FID, voice privacy, CSBK semantics and data traffic are unverified/unsupported by this test suite.
- These tests never alter D0/D1 headers, decoder sources, common CMake, `main.cpp`, firmware or UI; all generated build files must stay outside the tracked directory or in its ignored `build/`.
- Standards tables here are transformed into minimal numeric reference coefficients; no table graphics or substantial copyrighted text are redistributed.
