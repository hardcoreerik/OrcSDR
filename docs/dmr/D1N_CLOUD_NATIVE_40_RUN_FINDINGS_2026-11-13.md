# DMR D1-N — full 40-run native replay evidence and acceptance-criteria defect

**Date:** 2026-11-13 UTC / America-Los_Angeles.  
**Status:** **40/40 NATIVE REPLAYS MEASURED; D1-N OFFICIAL GATE FAIL (acceptance semantics defect)**.  
**Exact source commit:** `07e2b628b9e25e191b64a0087d598d8857bd06e4`.  
**Workflow:** [DMR D1 exact four-fixture native acceptance / GitHub run 43524922595](https://github.com/hardcoreerik/OrcSDR/actions/runs/43524922595).  
**Evidence:** GitHub Actions artifact `dmr-d1n-lead-native-evidence` (JSON reports, build/test logs, and source-pin evidence; **NO IQ binaries**); artifact ID `14084881889`, 21-day retention.  
**Workflow source:** [`.github/workflows/dmr-d1-lead-native-acceptance.yml`](../../.github/workflows/dmr-d1-lead-native-acceptance.yml).

## Execution — independently observed from artifact

The new workflow downloaded **the two original public GopherTrunk CS16 real-air recordings** at pinned commit `f06bfff493f47fdeb69807c8b08b2b32a253839f` and regenerated the **two exact private-REA-derived** synthetic negative controls using the current hash-pinned `negative_control_repro.py` (all four fixture SHA/length gates passed). IQ remained in GitHub runner temporary storage and was not uploaded.

The actual native `dmr_d1_replay_bridge` compiled in Release and Debug+ASan/UBSan; both CTest suites and D1-N Python unit tests passed. The original `d1n_matrix.py` executed all **4 fixtures × 5 chunk sizes (1,7,257,1024,4096) × 2 builds = 40 real native decoder replays**. All 40 rows have `status=MEASURED`, not mocked or assumed. For each fixture, exact ordered event-trace SHA-256 is **identical across all five chunks and across both separate binaries**.

| Verified fixture | IQ complex samples | Sync / burst events | Independently checked payloads | Source / talkgroup / color code | Negative false identity |
|---|---:|---:|---:|---|---:|
| `dmr-directmode-keyup` | 86,400 | 11 / 11 | 10 RS-checked bursts | `3024109 / 99 / 1` | n/a |
| `dmr-directmode-second-ptt` | 81,601 | 13 / 13 | 10 RS-checked bursts | `3024109 / 99 / 1` | n/a |
| `auto-negative-noise` | 96,000 | **0 / 0** | 0 | none | **0** |
| `auto-negative-am` | 96,000 | **0 / 0** | 0 | none | **0** |

No unverified identities were emitted. The two controls have empty source ID and talkgroup lists. Negative controls also produced no emitted tentative syncs. The bridge does not expose every *internally discarded* candidate; that count is **NOT MEASURED**, not zero.

## Why the official gate reports FAIL

The source-pinned real-air recordings were named *directmode* upstream, but our measured decoder observed **mobile-station (MS) sync words**:

- Keyup: `ms_data=10, ms_voice=1`, all `direct_ts1/2` and BS sync counts = 0.
- Second PTT: `ms_data=10, ms_voice=3`, all `direct_ts1/2` and BS sync counts = 0.
- All emitted checked bursts have `slot=null` by design. An MS sync word alone has **no unambiguous TDMA slot attribution**. D1-S intentionally avoids inventing a time slot from an unverified CACH prefix.
- `tests/dmr_replay_acceptance/real_iq_gate.py::assess_fixture` currently requires `slots_observed` on **every** positive recording and `slots_check_fec_bursts` on the first. Those expectations are **not applicable** to an observed MS-sync simplex capture. The resulting `FAIL` should **not** be repaired by guessing a slot or weakening the integrity requirements.
- The same harness only counts `direct_*` patterns when checking upstream-only `min_data_sync` and `min_voice_sync`, although these recordings actually use `ms_data`/`ms_voice`. This produced spurious `min_data_sync=0` disagreement; the observed MS data count is 10. The first recording has only **one** MS voice sync, below upstream's unverified minimum of two. Preserve this discrepancy in the report, not as a fabricated match.

The two positive fixtures failed their **slot attribution** assessments in Release and ASan, with no failed FEC/RS, wrong identity, source hash, chunk invariance or decoder-execution condition. The negative fixture assessments passed in both builds.

**Current D1-N official verdict: FAIL until acceptance semantics are corrected and the full job reruns.** Evidence supports accurate decoding of two MS simplex positives and zero false identities in these exact two negatives, but not a claim of universal field false-alarm reliability.

## Next tightly scoped correction — separate from decoder / hardware

1. In the D1-N acceptance code, require a TS1/TS2 slot only when the **actual verified signaling** supports one (explicit direct TS sync or independently corroborated BS CACH). Do not require a slot on `ms_data`/`ms_voice` alone.
2. Count upstream *direct-mode* sync expectations as comparisons to recorded **MS** patterns when applicable, with clear actual mode provenance. Preserve any real upstream disagreements as **non-gating review observations** rather than calling a correctly decoded packet invalid, or explicitly maintain a separate REVIEW state until that expectation is adjudicated. Never silently edit the original manifest.
3. Add regression tests to guarantee unknown slot stays `null` for MS frames; verified direct TS1/TS2 and corroborated BS time slots retain their stricter tests; missing/wrong identity or FEC/CRC failures still hard-fail.
4. Re-run all 40 native combinations and confirm all source pins, chunk hashes, independent integrity checks and zero false identities. Only mark D1-N **PASS** when the native gate status and report both say PASS.

**No D1 protocol, frontend, bridge, ESP-IDF, USB, P25, dashboard, Pluto or TX code was changed in this lead workflow.** The existing agent-owned acceptance code is left unchanged pending the dedicated semantic correction.

## Reproduction

In GitHub Actions: push changes to any D1-N source or the lead workflow on `feature/dmr-digital-voice-foundation`, or trigger `workflow_dispatch` for the lead workflow. Preserve the run URL, exact source SHA and artifact. The workflow executes real native Release + ASan CTests and the original D1-N 40-run matrix, with source-safe JSON/log artifacts and a job summary. A red result is evidence of an unmet gate, not an excuse to hide the discrepancy.
