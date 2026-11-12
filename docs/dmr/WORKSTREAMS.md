# DMR / Digital Voice — execution workstreams

**Integration branch:** `feature/dmr-digital-voice-foundation` (based on `docs/dmr-vision-tracking-20261111`).
**Source of scope:** [VISION.md](VISION.md) and [TRACKING.md](TRACKING.md). **As of:** 2026-11-11.
**Current status:** F0a event model and **F0b host-only two-slot storage** committed and locally tested. D0 protocol primitive source/tests committed; F0c on-device SD adapter, D1 DMR decoder, and import-apply integration remain open. Neither a live DMR receiver nor an on-device discovery system is implemented.

## One shared branch, exclusive folders

| Owner | Exclusively owned path | Task | Integration gate |
|---|---|---|---|
| Lead / ChatGPT | `components/orcsdr_radio_discovery/**`, `docs/dmr/TRACKING.md`, `docs/dmr/WORKSTREAMS.md` | F0 typed event/proof model, bounded storage design, phase integration | Host tests then later P25/SD/hardware regression |
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
- **F0b — Host implementation and recovery tests completed:** bounded three-provenance library, explicit `ORAD` v1 snapshot, two-slot CRC journal, known-field identity matching, conservative conflict handling and explicit keep-separate for incomplete identities. Debug, Release and ASan/UBSan host suites pass, including a POSIX file-backed `fsync` restart; see [validation evidence](F0B_HOST_VALIDATION_2026-11-11.md). **This does not establish a working Tab5 SD backend.**\n- **F0c — Next lead task:** qualify ESP-IDF SD `SlotIO` adapter, test power-loss recovery, measure internal RAM/PSRAM/stack and preserve P25 v2 compatibility. Do not add another radio owner.
- **D0 — Sandbox A:** ETSI-derived constants and host test source delivered in `components/orcsdr_dmr/**`; independent run and acceptance remain open.
- **D1 — After D0 acceptance:** standalone DMR Tier II 4FSK demod/framer and metadata on synthetic + appropriately pinned recorded IQ; no dashboard before host evidence.
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
