# OrcSDR DMR — Phase D0 protocol constants and interleaving

**Status:** host-only deterministic primitives; **not** a DMR demodulator, decoder, firmware component, or evidence of on-air reception.

Independent implementation of ETSI TS 102 361-1 V2.7.1 (2026-05), from the [ETSI air-interface specification](https://www.etsi.org/deliver/etsi_ts/102300_102399/10236101/02.07.01_60/ts_10236101v020701p.pdf):

| Primitive | Normative basis | Representation |
|---|---|---|
| Eight BS/MS/direct TS1/TS2 voice/data SYNC words | §9.1.1, Table 9.2 | `uint64_t` with low 48 bits valid, first transmitted bit in bit 47 |
| CACH TACT and 17-bit payload placement | §9.1.4, Table 9.5 and Fig. B.9, p. 146 | 3 octets, MSB-first, first transmitted bit at index 0 = `TX(23)` |
| TACT Hamming(7,4,3) parity / one-bit correction | Annex B.3.5, Table B.17 | 4 systematic information bits + 3 parity bits |
| BPTC(196,96) interleave/deinterleave | Annex B.1.1, Fig. B.1, Table B.2, Eq. B.1 | 196 bits in 25 octets, MSB-first; four low pad bits are zero |

## Key protocol choices

- SYNC words cover eight voice/data modes **only**. The MS reverse-channel and reserved SYNC values from Table 9.2 are not assigned voice/data `SyncKind` values.
- Fig. B.9 assigns seven TACT bits to transmitted positions `0,4,8,12,14,18,22` and the other 17 positions to the CACH payload. `AT,TC,LCSS(1),LCSS(0),H(2),H(1),H(0)` are kept in their transmitted order.
- The TACT generator is derived from the **ETSI Hamming generator matrix**, not from third-party source. Single-bit TACT correction is possible, but with a distance-3 code, **multiple corrupt bits may be miscorrected**. The 17 payload bits have no CRC at this layer. These primitives must **not** be used to upgrade a signal to `crc_valid_decode` or `site_confirmed`.
- For BPTC the extra reserved `R(3)` is encoded bit index 0. Eq. B.1 maps **encoded index `i` to transmitted index `(181 * i) % 196`**, while `(13 * tx) % 196` is the inverse. These functions only permute an *already FEC-coded* 196-bit block. Hamming row/column encoding, error recovery, CRC and burst extraction are out of scope. The unused low nibble of octet 24 must be zero; invalid input leaves output unchanged.
- A `BptcWire` needs exactly 25 bytes per input/output block. All operations use stack/compile-time memory only, no `new`, no allocator, no STL dynamic container, no I/O or receiver dependencies. In-place BPTC operations use a **25-byte stack snapshot**. Run-time CPU/stack characteristics on ESP32-P4 have **not** been measured.
- `CMakeLists.txt` is standalone and intentionally does not call `idf_component_register` or modify any other directory.

## Independent validation references

The [private REA conformance report](https://github.com/hardcoreerik/REA-Results/blob/research/dmr-competitor-20261111/comparisons/2026-11-11_dmr-competitor-landscape-2026-11-11/notes/PROTOCOL_CONSTANTS_CONFORMANCE.md) was consulted *only* to cross-check results. Its reported CACH positions and seven of its eight voice/data constants agree with the ETSI transcription. **One discrepancy was found:** it lists direct TS2 voice `0x7DFDD5F55D5F`; ETSI V2.7.1 Table 9.2 specifies `0x7DFFD5F55D5F`. D0 follows ETSI, and a static assertion plus a runtime test pin that value. The private report is research evidence, not source or executable test evidence. No competitor code or tables were copied.

## How to run / test

From the **OrcSDR repository root** with CMake ≥ 3.16 and a C++17 compiler:

```sh
cmake -S components/orcsdr_dmr -B /tmp/orcsdr-dmr-d0 -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/orcsdr-dmr-d0 --parallel
ctest --test-dir /tmp/orcsdr-dmr-d0 --output-on-failure
```

For the sanitizers on Linux with GCC or Clang:

```sh
cmake -S components/orcsdr_dmr -B /tmp/orcsdr-dmr-d0-asan \
  -DCMAKE_BUILD_TYPE=Debug -DORCSDR_DMR_SANITIZE=ON
cmake --build /tmp/orcsdr-dmr-d0-asan --parallel
ctest --test-dir /tmp/orcsdr-dmr-d0-asan --output-on-failure
```

The three independently registered CTest suites are `dmr_d0_sync`, `dmr_d0_cach`, and `dmr_d0_bptc`. Test assertions are explicit (active with `NDEBUG`): eight sync words and polarity pairs; Fig. B.9 mapping; 16 Hamming codewords plus all 112 single-bit corrections; 17 one-hot CACH payload bits; a pinned CACH byte vector; BPTC bijection and all 196 one-hot permutations; a fixed 25-byte interleave vector; in-place transformations and invalid pad/field handling.

**Acceptance boundary:** passing host unit tests establishes deterministic constants and mappings, not DMR 4FSK demodulation, time-slot acquisition, link-control decode, IQ performance, or firmware compatibility. D1 and subsequent integration are separate tracked phases.
