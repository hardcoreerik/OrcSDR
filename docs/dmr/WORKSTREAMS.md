# DMR / Digital Voice — execution workstreams

**Integration branch:** `feature/dmr-digital-voice-foundation` (based on `docs/dmr-vision-tracking-20261111`).
**Source of scope:** [VISION.md](VISION.md) and [TRACKING.md](TRACKING.md). **As of:** 2026-11-11.
**Current status:** F0a/F0b radio discovery journal host-tested, F0c ESP-IDF SD SlotIO host-delivered, D0 independently ETSI checked, and D1 native frontend/protocol/integration host-tested. **Important NEW evidence:** [public real-air IQ CI 43175761333](https://github.com/hardcoreerik/OrcSDR/actions/runs/43175761333) decoded both source-pinned direct-mode recordings through our own C++ 4FSK+FEC+LC decoder: 11/13 syncs, 9/10 FEC+integrity-valid bursts, radio `3024109`, TG `99`, CC `1`, chunk-invariant. All three D1-R/P/S agent commits are on branch; D1-S CACH cadence and D1-P 78-dibit measured pre-roll are now integrated with Release/sanitizer CI green. **Still open:** source-pinned noise/AM negative controls for full D1-R, exact SYNC single-burst robustness, ESP32-P4 RF/SD/memory, and P25 lossless migration. D2 scanner and M5GFX UI remain deferred.

## One shared branch, exclusive folders

| Owner | Exclusive write directory | Work | Gate |
|---|---|---|---|
| **Lead / integrator** | `components/orcsdr_dmr/d1_integration/**`, `docs/dmr/**`, DMR-specific `.github/workflows/**` | Integrate recovered first burst, trusted slot evidence, real-IQ results, CI and phase tracking | Real DMR IQ → integrity-validated metadata, honest no-sync/no-audio cases, no P25 regressions |
| **Agent 1 — D1-R** | `tests/dmr_replay_acceptance/**` | Run pinned recorded DMR IQ; refine acceptance harness strictly inside owned folder | Exact fixture SHA/bytes, observed vs upstream claims, negative controls and reproducible replay or BLOCKED |
| **Agent 2 — D1-P** | `components/orcsdr_dmr/d1_frontend/**` | Fixed-memory pre-acquisition dibit pre-roll API and synthetic first-burst tests | A full true first burst reconstructable with provenance/timestamps; chunk/lock/noise tests |
| **Agent 3 — D1-S** | `components/orcsdr_dmr/d1_protocol/**` | Cadence-backed CACH presence/slot confidence | D1-CACH-01 no longer promotes arbitrary prefix; dual BS slots and direct-mode regressions pass |

**READ ONLY for every agent:** other agents' folders, `d1_integration/**`, `docs/dmr/**`, production P25, firmware, `main`, and REA-Results (apart from authorized reads). The lead alone updates the shared tracker and composite D1 bridge.
**No overlap.** Sandbox agents must not edit `main.cpp`, `Roadmap.md`, `PROJECT_STATUS.md`, other agents' directories, RSA-Sandbox, or REA-Results. Do not merge or force-push. REA research is read-only to all implementation sessions. The lead coordinates later integration into firmware separately.

On this **one shared branch**, each agent must `git fetch`, check out `feature/dmr-digital-voice-foundation`, make small commits containing only its owned folder, `git pull --rebase origin feature/dmr-digital-voice-foundation` immediately before push, then `git push origin feature/dmr-digital-voice-foundation`. If the branch advanced or push is rejected, fetch and rebase again; never force-push. Report commit SHA, files and exact passing/failing tests. No agent may claim that a file in another folder is committed or complete.

## New D1-R/P/S three-agent completion and lead integration (2026-11-11)

| Phase | Agent committed SHA | Integrated state | Remaining acceptance |
|---|---|---|---|
| D1-R / recorded-IQ gate | `71b23111abf350623402839410a57c2ed4e119dd` | Fail-closed four-fixture gate implemented. **Lead's public CI has actually replayed both real-air positives and decoded validated ID/TG/CC.** | Exact source-pinned AM/noise controls and four-fixture comparison still not executed |
| D1-P / pre-roll | `3665e17ec1f29c715310f1a244fcb65473432e11` | 78 measured dibits returned, bridge updated, acquisition history counted separately | Clean isolated first-burst LC identity still not decoded in a specific synthetic test: one SYNC dibit error, exact-match D1-B decoder |
| D1-S / CACH confidence | `be23c18fd34d10979bccd0dc70c6f1b9992b7972` | Four consecutive, alternating BS CACH candidates required to confirm slot; formerly defective external conformance test updated; Release/ASan passing | Independent real BS/CACH cadence evidence and full device behavior |

**Lead integrated CI:** [D1 43175389210](https://github.com/hardcoreerik/OrcSDR/actions/runs/43175389210), [independent conformance 43175389267](https://github.com/hardcoreerik/OrcSDR/actions/runs/43175389267), and [actual public real-air IQ 43175761333](https://github.com/hardcoreerik/OrcSDR/actions/runs/43175761333). The first two test compilation and synthetic correctness; the third is separately hash-verified **actual recorded radio signal**, not mock data. See [REAL_IQ_PUBLIC_CAPTURE_ACCEPTANCE_2026-11-11.md](REAL_IQ_PUBLIC_CAPTURE_ACCEPTANCE_2026-11-11.md).

## Latest parallel delivery — all three agents complete

| Agent | Exclusive directory | Delivered | Independent CI / remaining gate |
|---|---|---|---|
| Replay acceptance | `tests/dmr_replay_acceptance/**` | Verified-format CS16/CF32 manifest harness, hash checks, strict JSONL v1 and negative-control gates | **16/16 Python tests passed**; pinned real IQ NOT RUN because the fixture bytes are not available to CI |
| ETSI conformance | `tests/dmr_protocol_conformance/**` | Rendered ETSI Golay oracle, 13 reference tests, five production-linked CTest suites, corrupt/late-entry probes | **13/13 Python; 5/5 Release + 5/5 ASan CTest passed**; confirmed **D1-CACH-01** slot confidence issue, Golay ambiguity resolved |
| P25 v2 migration | `tools/p25_discovery_migration/**` | Read-only profile parser and lossless *preview sidecar*, conflict/capacity checks | **38/38 Python tests passed**; actual migration cannot be applied until target model retains scan/hold/aliases/settings |

[Combined CI run 43158073956](https://github.com/hardcoreerik/OrcSDR/actions/runs/43158073956) validated the agents' production-linked protocol and Python suites. The new `d1_integration/tools/d1_replay_bridge.cpp` command is compiled against the actual D0/D1 sources; [CI 43158609068](https://github.com/hardcoreerik/OrcSDR/actions/runs/43158609068) passes Release and ASan with synthetic IQ and CLI negative tests. **Neither run had the private recorded IQ fixtures.**

**Next priority:** run the full four-fixture D1-R gate with exact pinned AM/noise negative controls; research tolerant-but-proven 48-bit synchronization with strict CRC/RS validation so single-burst identities survive one measured sync symbol error. Add independently sourced recorded IQ; qualify ESP32-P4 RF/SD/memory and preserve existing P25 profiles before any D2/Tab5 integration. **The two publicly pinned direct-mode real-air recordings were already successfully decoded in native host CI; do not revert their actual evidence to "not run".**

## Previous completed sandbox assignments (archived)

The original D0 constants, CSV/JSON import preview and P25/DMR/NXDN IQ manifest assignments are completed and **not currently assigned**. Their full briefs are preserved in this document's Git history. Later completed independent work includes the D1-A front-end, D1-B protocol decoder, F0c SD adapter, conformance suite, P25 v2 migration preview, and replay acceptance harness. The current live briefs are under [NEXT_PHASE_D1_ACCEPTANCE.md](NEXT_PHASE_D1_ACCEPTANCE.md); do not rerun the old assignments.

## Lead phase checkpoints

- **F0a — Implemented, host-tested only:** `components/orcsdr_radio_discovery/` exports typed `Observation`, `Origin`, `Proof`, protocol identity with explicit validity flags, receiver-location vs site identity separation, and `ObservationRing<Capacity>` with no post-init allocation. Host CMake test passed; release/NDEBUG test passed; ASan/UBSan local test passed. Not wired into firmware or SD.
- **F0b — Host implementation and recovery tests completed:** bounded three-provenance library, explicit `ORAD` v1 snapshot, two-slot CRC journal, known-field identity matching, conservative conflict handling and explicit keep-separate for incomplete identities. Debug, Release and ASan/UBSan host suites pass, including a POSIX file-backed `fsync` restart; see [validation evidence](F0B_HOST_VALIDATION_2026-11-11.md). **This does not establish a working Tab5 SD backend.**
- **F0c — Host adapter delivered:** `esp_idf/**` implements opt-in `SlotIO` and host fault tests. **Device compile, actual FAT/microSD power interruption, RAM/stack and P25 v2 compatibility remain unverified.** Do not add another radio owner.
- **D0 — ETSI and primitive code independently host-validated:** five D0/D1 production-linked conformance CTests plus 13 independent Python ETSI tests passed in [CI 43158073956](https://github.com/hardcoreerik/OrcSDR/actions/runs/43158073956). Rendered ETSI Golay tables match; remaining CACH cadence defect is a **D1 attribution issue**, not a failed Golay generator.
- **D1 — real-air host metadata now verified:** native 4FSK/DMR decoded two pinned direct-mode real-air IQ files and correctly produced radio/TG/CC with FEC/RS checks, identical at 257/4096 IQ chunks. D1-P recovered pre-roll and D1-S CACH confidence are now integrated, but **full four-fixture D1-R, first-burst near-sync tolerance, other real-air modes and ESP32-P4 remain open.** No P25, firmware or M5GFX changes.
- **D2 — After D1 acceptance:** radio ownership, location-based scan, last-heard log, import migration, UI, and no-network acceptance. Separate firmware build and physical RF gates.
- **D3+:** offline indexes, recorder/replay, NXDN, weak-signal survey and trunking in tracked order.

## How to run the lead's current F0a tests

From OrcSDR root (Linux/macOS/WSL with CMake and C++17 compiler):

```sh
cmake -S components/orcsdr_radio_discovery -B build-radio-discovery -DCMAKE_BUILD_TYPE=Release
cmake --build build-radio-discovery
ctest --test-dir build-radio-discovery --output-on-failure
```

The F0a test validates only evidence semantics, release assertion behavior and ring bounds. It is **not** a DMR decode test, an SD persistence test or a device benchmark. Record all future phase closures with exact test command, log, SHA, environment and evidence level.
