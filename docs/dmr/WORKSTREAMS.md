# DMR / Digital Voice — execution workstreams

**Integration branch:** `feature/dmr-digital-voice-foundation` (based on `docs/dmr-vision-tracking-20261111`).
**Source of scope:** [VISION.md](VISION.md) and [TRACKING.md](TRACKING.md). **As of:** 2026-11-11.
**Current status:** F0a/F0b host storage established, F0c SD adapter host source committed. D1-A frontend and D1-B protocol are now connected by **D1-C**, and [Release/ASan synthetic IQ-to-identity tests passed](https://github.com/hardcoreerik/OrcSDR/actions/runs/43156539511). **Real-air IQ, first-acquired-burst decoding, ESP32-P4 SD hardware and profile migration remain open.** Neither a live DMR receiver nor an on-device discovery system is implemented. See [D1/F0c integration gate](D1_INTEGRATION_GATE_2026-11-11.md).

## One shared branch, exclusive folders

| Owner | Exclusively owned path | Task | Integration gate |
|---|---|---|---|
| Lead / ChatGPT | `components/orcsdr_radio_discovery/**` (excluding agent-owned `esp_idf/**`), `components/orcsdr_dmr/d1_integration/**`, `docs/dmr/{TRACKING,WORKSTREAMS,D1_INTEGRATION_GATE_2026-11-11}.md`, `.github/workflows/dmr-d1-integration.yml` | F0a/b storage, D1-C integration, verification and phase tracking | Real D0+D1 end-to-end host CI then private IQ/device gates |
| Sandbox A | `components/orcsdr_dmr/**` | D0 standards-derived DMR constants, CACH, sync/FEC test oracles | Host tests, ETSI citations, no copied competitor source |
| Sandbox B | `tools/radio_profile_import/**` | CFG-01/03 portable CSV/JSON preview, validation and duplicate/conflict tests | Offline command-line examples and safe no-write dry-run |
| Sandbox C | `tests/digital_voice_replay/**` | Reproducible IQ fixture manifest/converters and host verification harness | Verify encoding, hashes, ground truth; no sensitive IQ committed |

**No overlap.** Sandbox agents must not edit `main.cpp`, `Roadmap.md`, `PROJECT_STATUS.md`, other agents' directories, RSA-Sandbox, or REA-Results. Do not merge or force-push. REA research is read-only to all implementation sessions. The lead coordinates later integration into firmware separately.

On this **one shared branch**, each agent must `git fetch`, check out `feature/dmr-digital-voice-foundation`, make small commits containing only its owned folder, `git pull --rebase origin feature/dmr-digital-voice-foundation` immediately before push, then `git push origin feature/dmr-digital-voice-foundation`. If the branch advanced or push is rejected, fetch and rebase again; never force-push. Report commit SHA, files and exact passing/failing tests. No agent may claim that a file in another folder is committed or complete.

## Copy-ready parallel assignments

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
- **D0 — Sandbox A:** ETSI-derived constants and host test source delivered in `components/orcsdr_dmr/**`; independent run and acceptance remain open.
- **D1 — Integrated synthetic host checkpoint:** `d1_frontend/**` and `d1_protocol/**` now run through `d1_integration/**` with eight CRC/RS-gated group identity events from nine synthesized full-LC bursts. [CI Release + ASan](https://github.com/hardcoreerik/OrcSDR/actions/runs/43156539511) passed. **Acquisition still loses the first burst**, and pinned real-air IQ, ETSI Golay ambiguity and device profiling remain unresolved. Agent 1 owns independent fixture replay acceptance, Agent 2 independent protocol conformance, Agent 3 non-destructive P25 migration; their assigned new folders do not overlap this integration work.
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
