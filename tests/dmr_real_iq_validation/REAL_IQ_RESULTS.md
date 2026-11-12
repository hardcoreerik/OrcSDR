# Real-air DMR IQ — source verification and native D1 acceptance

**Repository:** `hardcoreerik/OrcSDR` · **branch:** `feature/dmr-digital-voice-foundation` · **Git SHA inspected:** `3665e17ec1f29c715310f1a244fcb65473432e11` (pre-delivery, not a decoder execution SHA). **Source repository:** `MattCheramie/GopherTrunk` at `f06bfff493f47fdeb69807c8b08b2b32a253839f`.

**What was done:** Four public objects were retrieved in a connected GitHub execution context as base64-encoded binary file content. Each object was decoded and its size, Git blob SHA-1 (including the `blob <length>\0` prefix) and independently calculated raw SHA-256 were checked against source/manifest values. **All four passed.** Binary data could not be transferred into this run's native CMake/C++ environment (the sandbox has no outbound GitHub DNS/network). **No public or private IQ was replayed through the native bridge.** Thus `ORCSDR_OBSERVED` is NOT MEASURED throughout. The four public recordings are real-air *channelized* 48 kS/s IQ, not untouched SDR captures.

## A — DMR simplex key-up

| Field | Value / provenance |
|---|---|
| Fixture | `dmr-directmode-keyup` / `DMR/dmr-directmode-446500-keyup-48k.cs16` |
| Encoding | CS16 LE, I/Q interleaved, 48,000 complex samples/s |
| Byte length / complex samples | **345,600 / 86,400** (source verified) |
| Pinned Git blob SHA-1 | `e305eac29985ff647b8aba0434fee8ab7db81f15` — **MATCH** |
| Computed SHA-256 | `b09d35659bcbd242a0d23fad275fb38b0e84365a2bb7dcf067677b5856571bf6` |
| UPSTREAM_EXPECTED | 446.500 MHz direct mode, source `3024109`, talkgroup `99`, color code `1`, min 8 data syncs and 2 voice syncs |
| ORCSDR_OBSERVED | **NOT MEASURED** — zero native real-IQ executions |
| Sync/bursts, Golay, BPTC, CRC/RS, color/slot/identity | **NOT MEASURED** |
| Initial acquisition, gap/reacquisition, CFO/timing | **NOT MEASURED / NOT EXPOSED BY CURRENT API** as applicable |
| Event chunk invariance (1, 7, 257, 1024, 4096) | **NOT ASSESSED** |
| Acceptance | **BLOCKED — NATIVE IQ UNAVAILABLE** |

## B — DMR second simplex PTT

| Field | Value / provenance |
|---|---|
| Fixture | `dmr-directmode-second-ptt` / `DMR/dmr-directmode-446500-ptt2-48k.cs16` |
| Encoding | CS16 LE, I/Q interleaved, 48,000 complex samples/s |
| Byte length / complex samples | **326,404 / 81,601** (source verified) |
| Pinned Git blob SHA-1 | `b7d91747030e55f68637edb2198476047191d84a` — **MATCH** |
| Computed SHA-256 | `dad93254a12416f640d61d65f9a5335e955c49ad92217f75dc322257ed421880` |
| UPSTREAM_EXPECTED | Second-PTT/symbol-phase regression, no independently verified metadata IDs |
| ORCSDR_OBSERVED | **NOT MEASURED** — zero native real-IQ executions |
| Sync/bursts, Golay, BPTC, CRC/RS, color/slot/identity | **NOT MEASURED** |
| Burst gap/first-lock behavior, CFO/timing | **NOT MEASURED / NOT EXPOSED BY CURRENT API** as applicable |
| Event chunk invariance | **NOT ASSESSED** |
| Acceptance | **BLOCKED — NATIVE IQ UNAVAILABLE** |

## C — Tier III control (exploratory)

| Field | Value / provenance |
|---|---|
| Fixture | `dmr-tier3-control` / `DMR/dmr-t3-cc.cfile` |
| Encoding | CF32 LE, I/Q interleaved, 48,000 complex samples/s |
| Byte length / complex samples | **499,200 / 62,400** (source verified) |
| Pinned Git blob SHA-1 | `7a119ec16e5c6412488f2ed112b2c5b85cdd799f` — **MATCH** |
| Computed SHA-256 | `08d1697bc2c53721be1c3071f34760d03f3274bc1b85432b48273421495ffbf0` |
| UPSTREAM_EXPECTED | Tier III control, 12 CRC-valid CSBK, 8 ALOHA (unverified as OrcSDR) |
| ORCSDR_OBSERVED | **NOT MEASURED** — zero native real-IQ executions |
| Sync/bursts/CRC/slot/identity/CFO | **NOT MEASURED** |
| Tier III decoder coverage | **NOT ASSESSED** — complete Tier III is not currently promised |
| Acceptance | **BLOCKED — NATIVE IQ UNAVAILABLE** (exploratory only) |

## D — DMR voice terminator (exploratory)

| Field | Value / provenance |
|---|---|
| Fixture | `dmr-tier3-voice-terminator` / `DMR/dmr-voice-term.cfile` |
| Encoding | CF32 LE, I/Q interleaved, 48,000 complex samples/s |
| Byte length / complex samples | **537,600 / 67,200** (source verified) |
| Pinned Git blob SHA-1 | `91620e07e5350af6bce85d75152c23c62bfed253` — **MATCH** |
| Computed SHA-256 | `5b442ae37a40d1344a7be92130812ae099c9f683a21ee338c70e5d7599efd0c9` |
| UPSTREAM_EXPECTED | Talkgroup `24`, source `4209000`, min 8 valid voice terminators; *upstream assertion only* |
| ORCSDR_OBSERVED | **NOT MEASURED** — zero native real-IQ executions |
| Sync/bursts/RS/LC/slot/identity/CFO | **NOT MEASURED** |
| Acceptance | **BLOCKED — NATIVE IQ UNAVAILABLE** (exploratory only) |

## Negative controls and timing/performance

Pinned `auto-negative-noise` and `auto-negative-am` are private synthetic REA-Results fixtures, **not publicly downloaded** in this run. False identities, truncated/corrupt IQ handling, resets, discontinuity, total native execution time, CPU and RSS: **NOT MEASURED**. Native bridge's current JSONL does not expose every DSP-internal quantity such as precise CFO, symbol error rate, acquisition onset, Golay correction count or burst reacquisition; label those **NOT EXPOSED BY CURRENT API**, not zero.

**Actual executed check:** local isolated Python tests for `replay_matrix.py`: **10/10 PASS**. These are orchestration tests with synthetic byte strings and a *stubbed external acceptance import*, not native DMR tests. `python3 -m py_compile replay_matrix.py` passed. Public GitHub hashes were independently recomputed in a separate connected execution context. Complete stdout/stderr native replay logs do not exist because native replay did not run; future execution uses the external results directory documented in README.
