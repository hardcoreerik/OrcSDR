# D0 / D1 independent conformance findings — 2026-11-11

**Review target:** `components/orcsdr_dmr` from OrcSDR branch `feature/dmr-digital-voice-foundation`, inspected at remote SHA `1bd670c42b506b5a498b3786cc4e82e9db86b341` (review start). All implementation files read-only. New tests are under `tests/dmr_protocol_conformance/` only.

## Finding D1-CACH-01 — Unverified CACH prefix can be reported as slot attribution

**Status:** mathematically **confirmed** and traced to D1 source code. New production-linked CTest repro is included but **NOT executed against live D1 binary in this sandbox**. **Severity:** moderate for slot fidelity, particularly corrupted/truncated captures; **not proof of an invalid radio ID**.

**Issue:** `Decoder::scan()` in `d1_protocol/src/d1.cpp` (lines 315–329 at review SHA) reads the 12 dibits immediately before a matching BS SYNC and calls `unpack_cach()` without requiring a credible 30 ms BS CACH cadence or any other evidence that those 24 bits *are actually a CACH*. It sets `cach_present = true`, `slot_from_cach = true` and `slot=1/2` on a successful Hamming TACT interpretation. D0's `unpack_cach()` delegates to the perfect Hamming(7,4,3) decoder, which **accepts all 128 possible 7-bit values**: any received value is either an exact codeword or exactly one bit away from one. Its 17 CACH payload bits are not checked for integrity here. Therefore the parser's return value alone cannot distinguish a genuine CACH from an unrelated 24-bit prefix.

**Exact minimal reproduction:**

1. Use the `stream` C++ suite supplied here (`dmr_conformance_stream`). Generate 12 dibits of `0` intended as a **non-CACH, arbitrary prefix**, followed by a 132-dibit BS voice burst whose dibits 54–77 contain ETSI BS voice sync `0x755FD7DF75F7`; other burst dibits zero. Feed in 1-dibit chunks.
2. At burst end, D1 finds SYNC and reads those 12 zero prefix dibits. CACH TACT positions contain codeword `0x00` (AT=0, TC=0, LCSS=0, parity zero), so `unpack_cach()` returns true with `corrected=0`.
3. D1 emits a `sync_only` event with **`cach_present=true`, `slot_from_cach=true`, `slot=1`**, even though the test input never established a real CACH. The C++ suite pins this existing behavior (it deliberately prints `KNOWN DEFECT D1-CACH-01`).
4. Run `python3 -m unittest discover -s tests/dmr_protocol_conformance -p 'test_etsi_oracle.py' -v` and observe `test_tact_every_seven_bit_word_looks_valid` PASS; the check independently proves the underlying covering-radius-one limitation.

**Root cause:** neither a TACT syndrome=0 nor an inferred one-bit correction **authenticates that the 24-bit window is CACH**. The D1 field `cach_present` is stronger than the evidence produced by `unpack_cach`. It also means a convincing-looking `slot=1` can attach to a sync-only BS burst in truncated/poor-quality IQ replays. Identity still requires later BPTC/RS validation; these tests do not show false radio IDs for this defect.

**Recommended future fix (outside this owned test folder):** distinguish `cach_decoded` from `cach_cadence_validated`, avoid treating every preceding 24 bits as an authenticated CACH, track BS 144-dibit burst cadence and guard/gap expectations, and mark slot as **unconfirmed** when CACH alignment is not corroborated. Add a test showing no false slot confirmation from a random preceding prefix. A full repair may also need a lock/timing model in the upstream D1 framer.

## Golay(20,8) matrix transcription ambiguity — **resolved, no defect confirmed**

**Priority inspected:** highest. Rendered ETSI V2.6.1 and V2.7.1 page index 136 (both printed page 137). The visually printed 8×20 matrix is the same in both revisions. The row parity words are:

`3DA D99 6CD 367 DC6 A97 93E 8EB`

The previously reported discrepancy is from machine extraction/transcription, not an actual discrepancy between these rendered tables. The independently generated 256-word codebook has minimum distance eight, as required; `CC=2,type=1` yields `0x21E26`. D1's checked-in eight generator parity words match these. **No basis to change production Golay coefficients.** Production-linked C++ assertions include triple corruption, four-bit rejection and field extraction.

## Other checks / evidence levels

| Area | Independent result | Production execution |
|---|---|---|
| All eight sync constants | Match ETSI rendered Table 9.2, including direct TS2 voice `0x7DFFD5F55D5F` | C++ assertions written, not run here |
| Slot Type 10+10 bit extraction | Matches ETSI Figure 6.5 placement for 20-bit Golay; all 256 codewords covered by C++ test | Not run here |
| D0 CACH and BPTC | TACT parity/bit-placement, 181i mod 196 permutation, Hamming product rows/cols verified mathematically | C++ assertions written, not run here |
| CRC / RS | B.18 GF(256) parity and roots, B.3.8 CCITT zero-init/final inversion, B.21 masks tested against reference vectors | C++ D1 gate test written, not run here |
| Dual slot | Two independent BS slots with deliberate CACH, multi-chunk/reset tests | C++ case written, not run here |
| Full LC identity | Tables 7.1 / 7.2 PF/R/FLCO/FID and 24-bit group/private source/destination | C++ false-identity cases written, not run here |
| Late entry | 128-bit independent BPTC checksum, Hamming parity, 4 fragment order plus corrupted/missing cases | C++ case written, not run here |
| Native hardware / recorded IQ | Not tested | Not run |

**Defect count:** 1 code-path/mathematics-confirmed attribution problem; **0 runtime-confirmed production defects** because a complete D1 binary was unavailable for linked execution. No evidence establishes false identity emission, Golay generator errors, or overall RF performance problems. Follow-up validation must run the actual five CTest targets against the current repository source, then capture logs and any failing corpus before modifying decoders.
