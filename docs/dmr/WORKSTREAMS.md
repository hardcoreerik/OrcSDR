# DMR / Digital Voice — execution workstreams

**Integration branch:** `feature/dmr-digital-voice-foundation` (based on `docs/dmr-vision-tracking-20261111`).
**Source of scope:** [VISION.md](VISION.md) and [TRACKING.md](TRACKING.md). **As of:** 2026-11-11.
**Current status:** F0a/F0b host storage, F0c host SD adapter, and D1-A/B/C synthetic IQ→validated metadata implemented. All three latest agent handoffs are committed: independent ETSI conformance, real-IQ acceptance driver, and non-destructive P25 v2 migration preview. The lead also committed a **production-linked JSONL replay executable** with passing Release/ASan CLI and decoder CI. **Real-air IQ, CACH-cadence defect D1-CACH-01, first-burst recovery, ESP32-P4 SD/RF hardware and lossless P25 migration remain open.** Nothing is wired into live firmware. [D1-C replay instructions](../../components/orcsdr_dmr/d1_integration/README.md).

## One shared branch, exclusive folders

| Owner | Exclusively owned path | Task | Integration gate |
|---|---|---|---|
| Lead / ChatGPT | `components/orcsdr_radio_discovery/**` (excluding agent-owned `esp_idf/**`), `components/orcsdr_dmr/d1_integration/**`, `docs/dmr/{TRACKING,WORKSTREAMS,D1_INTEGRATION_GATE_2026-11-11}.md`, `.github/workflows/dmr-d1-integration.yml` | F0a/b storage, D1-C integration, verification and phase tracking | Real D0+D1 end-to-end host CI then private IQ/device gates |
| Sandbox A | `components/orcsdr_dmr/**` | D0 standards-derived DMR constants, CACH, sync/FEC test oracles | Host tests, ETSI citations, no copied competitor source |
| Sandbox B | `tools/radio_profile_import/**` | CFG-01/03 portable CSV/JSON preview, validation and duplicate/conflict tests | Offline command-line examples and safe no-write dry-run |
| Sandbox C | `tests/digital_voice_replay/**` | Reproducible IQ fixture manifest/converters and host verification harness | Verify encoding, hashes, ground truth; no sensitive IQ committed |

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

## Copy-ready parallel assignments (historical; already completed)


### A — DMR standards and protocol oracle

> Work on `hardcoreerik/OrcSDR` branch `feature/dmr-digital-voice-foundation`. You own ONLY `components/orcsdr_dmr/**`. Complete D0: independently implement ETSI TS 102 361-1 v2.7.1 sync constants (8 patterns), CACH TACT map, and BPTC(196,96) permutation with pure, fixed-memory C++ and host tests. Read private `REA-Results` branch `research/dmr-competitor-20261111` `PROTOCOL_CONSTANTS_CONFORMANCE.md` as an oracle; derive code from ETSI, never copy competitor source. Compile/test, commit and push only your folder to this shared branch; pull --rebase before push. Report exact tests, status, commit. No receiver or firmware integration.

### B — Offline profile migration preview

> Work on `hardcoreerik/OrcSDR` branch `feature/dmr-digital-voice-foundation`. You own ONLY `tools/radio_profile_import/**`. Build a host-only, offline import **preview** for versioned OrcSDR JSON and conventional CSV channel lists: field mapping, warnings, invalid rows, source provenance and deterministic duplicate/conflict classification (not filename-based; frequency alone insufficient). No automatic overwrite, no network use, no claim of universal codeplug/CPS compatibility. Include fixtures/tests/CLI instructions; do not modify production P25 profiles. Commit and push only your folder; pull --rebase before push. Report exact tests and SHA.

### C — P25/DMR/NXDN fixture and replay verification

> Work on `hardcoreerik/OrcSDR` branch `feature/dmr-digital-voice-foundation`. You own ONLY `tests/digital_voice_replay/**`. Read private `REA-Results` branch `chatgpt/digital-voice-rsa-rea-2026-11-01` `inbox/fixtures/digital-voice/README.md`. Create host-only fixture manifest, SHA/byte-length/format verification and optional conversion helpers for **CF32 LE** versus **CS16 LE**, at 48 kS/s. Preserve source attribution and upstream-vs-our-results labels. Do NOT commit IQ captures, executables, recordings or proprietary binaries. Include tests/repro steps; commit and push only your folder; pull --rebase before push. Report SHA and limitations.

## Lead phase checkpoints

- **F0a — Implemented, host-tested only:** `components/orcsdr_radio_discovery/` exports typed `Observation`, `Origin`, `Proof`, protocol identity with explicit validity flags, receiver-location vs site identity separation, and `ObservationRing<Capacity>` with no post-init allocation. Host CMake test passed; release/NDEBUG test passed; ASan/UBSan local test passed. Not wired into firmware or SD.
- **F0b — Host implementation and recovery tests completed:** bounded three-provenance library, explicit `ORAD` v1 snapshot, two-slot CRC journal, known-field identity matching, conservative conflict handling and explicit keep-separate for incomplete identities. Debug, Release and ASan/UBSan host suites pass, including a POSIX file-backed `fsync` restart; see [validation evidence](F0B_HOST_VALIDATION_2026-11-11.md). **This does not establish a working Tab5 SD backend.**
- **F0c — Host adapter delivered:** `esp_idf/**` implements opt-in `SlotIO` and host fault tests. **Device compile, actual FAT/microSD power interruption, RAM/stack and P25 v2 compatibility remain unverified.** Do not add another radio owner.
- **D0 — ETSI and primitive code independently host-validated:** five D0/D1 production-linked conformance CTests plus 13 independent Python ETSI tests passed in [CI 43158073956](https://github.com/hardcoreerik/OrcSDR/actions/runs/43158073956). Rendered ETSI Golay tables match; remaining CACH cadence defect is a **D1 attribution issue**, not a failed Golay generator.
- **D1 — End-to-end synthetic IQ and replay CLI:** native D0/D1 components and the CLI pass Release/ASan integration CI. The replay harness can now drive the actual receiver from verified local CS16/CF32 IQ. **Real-air fixture decoding NOT RUN here; first acquisition burst missed; CACH-derived BS slot provenance needs cadence validation; no ESP32-P4 test.**
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
