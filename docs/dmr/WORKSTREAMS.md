# DMR / Digital Voice — execution workstreams

**Integration branch:** `feature/dmr-digital-voice-foundation` (based on `docs/dmr-vision-tracking-20261111`).
**Source of scope:** [VISION.md](VISION.md) and [TRACKING.md](TRACKING.md). **As of:** 2026-11-11.
**Current status:** F0a/F0b fixed-memory discovery journal host-tested, F0c ESP-IDF microSD SlotIO host-delivered (hardware unverified), D0 independently ETSI-host-validated, D1-A/B/C integrated synthetic IQ→validated metadata with real host replay CLI in Release/ASan CI. The latest three agents delivered 16 replay-harness, 13 ETSI oracle, five CTest and 38 P25 preview tests. **Remaining D1 blockers:** pinned real-air IQ has not been decoded, initial acquisition burst is lost, and D1-CACH-01 permits uncorroborated CACH slot attribution. These form the [next-phase parallel assignments](NEXT_PHASE_D1_ACCEPTANCE.md). D2 and M5GFX are deferred.

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

## Latest parallel delivery — all three agents complete

| Agent | Exclusive directory | Delivered | Independent CI / remaining gate |
|---|---|---|---|
| Replay acceptance | `tests/dmr_replay_acceptance/**` | Verified-format CS16/CF32 manifest harness, hash checks, strict JSONL v1 and negative-control gates | **16/16 Python tests passed**; pinned real IQ NOT RUN because the fixture bytes are not available to CI |
| ETSI conformance | `tests/dmr_protocol_conformance/**` | Rendered ETSI Golay oracle, 13 reference tests, five production-linked CTest suites, corrupt/late-entry probes | **13/13 Python; 5/5 Release + 5/5 ASan CTest passed**; confirmed **D1-CACH-01** slot confidence issue, Golay ambiguity resolved |
| P25 v2 migration | `tools/p25_discovery_migration/**` | Read-only profile parser and lossless *preview sidecar*, conflict/capacity checks | **38/38 Python tests passed**; actual migration cannot be applied until target model retains scan/hold/aliases/settings |

[Combined CI run 43158073956](https://github.com/hardcoreerik/OrcSDR/actions/runs/43158073956) validated the agents' production-linked protocol and Python suites. The new `d1_integration/tools/d1_replay_bridge.cpp` command is compiled against the actual D0/D1 sources; [CI 43158609068](https://github.com/hardcoreerik/OrcSDR/actions/runs/43158609068) passes Release and ASan with synthetic IQ and CLI negative tests. **Neither run had the private recorded IQ fixtures.**

**Next priority:** execute pinned direct-mode IQ locally with the real replay command, record exact source ID/talkgroup/color/sync/error metrics, and fix the unverified-CACH-slot attribution before trusting it in discovery storage. D1 acquisition currently misses the first burst; assess and implement fixed-memory pre-roll after the real replay exposes the precise sensitivity limits. Device SD qualification and P25 migration apply remain separate acceptance gates.

## Previous completed sandbox assignments (archived)

The original D0 constants, CSV/JSON import preview and P25/DMR/NXDN IQ manifest assignments are completed and **not currently assigned**. Their full briefs are preserved in this document's Git history. Later completed independent work includes the D1-A front-end, D1-B protocol decoder, F0c SD adapter, conformance suite, P25 v2 migration preview, and replay acceptance harness. The current live briefs are under [NEXT_PHASE_D1_ACCEPTANCE.md](NEXT_PHASE_D1_ACCEPTANCE.md); do not rerun the old assignments.

## Lead phase checkpoints

- **F0a — Implemented, host-tested only:** `components/orcsdr_radio_discovery/` exports typed `Observation`, `Origin`, `Proof`, protocol identity with explicit validity flags, receiver-location vs site identity separation, and `ObservationRing<Capacity>` with no post-init allocation. Host CMake test passed; release/NDEBUG test passed; ASan/UBSan local test passed. Not wired into firmware or SD.
- **F0b — Host implementation and recovery tests completed:** bounded three-provenance library, explicit `ORAD` v1 snapshot, two-slot CRC journal, known-field identity matching, conservative conflict handling and explicit keep-separate for incomplete identities. Debug, Release and ASan/UBSan host suites pass, including a POSIX file-backed `fsync` restart; see [validation evidence](F0B_HOST_VALIDATION_2026-11-11.md). **This does not establish a working Tab5 SD backend.**
- **F0c — Host adapter delivered:** `esp_idf/**` implements opt-in `SlotIO` and host fault tests. **Device compile, actual FAT/microSD power interruption, RAM/stack and P25 v2 compatibility remain unverified.** Do not add another radio owner.
- **D0 — ETSI and primitive code independently host-validated:** five D0/D1 production-linked conformance CTests plus 13 independent Python ETSI tests passed in [CI 43158073956](https://github.com/hardcoreerik/OrcSDR/actions/runs/43158073956). Rendered ETSI Golay tables match; remaining CACH cadence defect is a **D1 attribution issue**, not a failed Golay generator.
- **D1 — next active phase (D1-R/P/S):** synthetic D0/D1 IQ-to-metadata and actual CLI pass Release/ASan host CI, but **real-air fixture decoding NOT RUN; first acquisition burst missed; CACH-derived BS slot provenance requires cadence validation; no ESP32-P4 test.** The three new agents own separate replay, pre-roll and protocol folders. See [exit criteria](NEXT_PHASE_D1_ACCEPTANCE.md).
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
