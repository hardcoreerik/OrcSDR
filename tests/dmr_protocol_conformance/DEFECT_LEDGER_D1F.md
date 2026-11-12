# D1-F defect ledger — adversarial audit

Target for the new test suite: D1 decoder on
`feature/dmr-digital-voice-foundation`, inspected SHA
`e6905ba4cdc283d79cf905c37f60c7e79e58b4f6`.
The ledger separates tested evidence from hypotheses and unexecuted cases.
**No production decoder changes are authorized by this agent.**

| ID | Severity | Status | Finding / exact reproduction |
|---|---|---|---|
| D1-F-01 | High if reproduced | **Not confirmed; linked test pending** | One corrupted bit in an otherwise valid full burst *must* retain the same source/TG only after BPTC + RS/CRC. `test_false_sync.cpp::test_one_bit_all_payloads`, exactly 4 syncs × 5 packet types × 48 positions. An incorrect identity or unchecked fuzzy event is an immediate failure. |
| D1-F-02 | High if reproduced | **Not confirmed; linked test pending** | At least two bad sync bits must create **zero** events even with valid FEC/RS. `test_multibit_exhaustive` enumerates 4×(1,128+17,296) cases. A tentative event on a two-/three-bit hit is a policy defect. |
| D1-F-03 | Critical if reproduced | **Not confirmed; linked test pending** | `test_integrity_gate` invalidates RS/CRC **before recomputing fully valid BPTC**, then flips one SYNC bit. Any event, regardless of evidence label, violates the new fuzzy acceptance gate. Also forces invalid R(3) BPTC and 4-bit Golay damage. |
| D1-F-04 | High if reproduced | **Not confirmed; linked test pending** | `test_voice_and_geometry` puts exact/one-bit corrupted VOICE sync inside a valid DATA payload. Exact VOICE can be `sync_only`, but one-bit VOICE must not produce an authenticated DATA event. |
| D1-F-05 | Medium | **Residual evidence limit; not a coding defect** | CACH 7-bit TACT is a perfect Hamming covering code: arbitrary 24-bit prefixes are only tentative. `test_cach_and_streaming` probes zero prefix, third-burst TACT bit flip, missing 11 dibits, four alternating CACHs and decoder reset. An uncorroborated slot promotion would be a new defect. |
| D1-F-06 | Informational | **Architectural limitation, confirmed by standards** | A deliberately forged *fully encoded* DMR packet can carry an invented source ID. RS/CRC check coding integrity, **not transmitter authentication**. This audit cannot establish RF identity authenticity. |
| D1-F-07 | Medium | **Coverage limitation** | Randomized hard dibits and quantized AM-envelope proxies do **not** reproduce exact pinned negative-control IQ. The separate D1-N acceptance gate must provide SHA-pinned real input results. |

## Source inspection evidence

The current `d1_protocol/src/d1.cpp::Decoder::scan` selects DATA patterns at
**exactly one differing bit**, requires `decode_slot_type`, permits only data
types 1/2/3/6/11, then checks `decode_bptc` and
`check_full_lc`/`check_crc16` before emission. CACH candidate cadence is
considered only **after** this gate; fuzzy voice is excluded. A static code
review shows the intended checks in order. **This is not a runtime PASS.**

## Reproduction

```sh
cmake -S tests/dmr_protocol_conformance -B build/dmr-d1f \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build/dmr-d1f --parallel
ctest --test-dir build/dmr-d1f -R 'dmr_false_sync_' \
    --output-on-failure -V > ./dmr-d1f-ledger.log 2>&1
```

The C++ report prints compiler, source commit SHA, seed, total candidate
cases, accumulated bits, event/identity counts, and elapsed runtime.
If a regression fails, preserve the **exact printed line, source SHA,
compiler and full log** before attempting a decoder fix in a separately
owned workstream. Debug/Sanitizer reproduction commands and the full corpus
layout are documented in `README_D1F.md`.
