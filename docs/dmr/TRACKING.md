# DMR / Digital Voice — development tracker

**Status:** F0a/F0b host validated; F0c host adapter delivered (device unverified); D0 independent ETSI conformance passed; D1-A/B/C synthetic IQ→metadata and the real-IQ replay CLI passed host CI. **D1-R recorded IQ, D1-P first-burst recovery and D1-S corroborated CACH slot attribution are the active parallel gates.** ESP32-P4 hardware and lossless migration remain open. **Created:** 2026-11-11.
**Product requirements:** [VISION.md](VISION.md). **Current implemented truth:** [PROJECT_STATUS.md](../../PROJECT_STATUS.md).
**Evidence:** [DMR RSA/REA research branch](https://github.com/hardcoreerik/REA-Results/tree/research/dmr-competitor-20261111), plus the older [digital-voice fixture branch](https://github.com/hardcoreerik/REA-Results/tree/chatgpt/digital-voice-rsa-rea-2026-11-01). Both are research artifacts, not implementation acceptance.

> Priority is not completion status. Every item below is **NOT STARTED** unless explicitly marked otherwise. An imported directory hit is never a verified RF decode. No network connection is needed for the promised core workflows.

## Active implementation checkpoint — D1 host acceptance (2026-11-11)

**Integration branch:** [`feature/dmr-digital-voice-foundation`](https://github.com/hardcoreerik/OrcSDR/tree/feature/dmr-digital-voice-foundation).
[Parallel workstreams and restart instructions](WORKSTREAMS.md) set exclusive folder ownership for additional AI sessions.

- **Done, host-only:** `components/orcsdr_radio_discovery/` contains the header-only typed `Observation` contract (origin, proof level, protocol identity with known-field flags, receiver location, relative measurement fields) and a bounded `ObservationRing`. Tested locally using CMake/CTest, a Release build with `NDEBUG`, and AddressSanitizer/UndefinedBehaviorSanitizer; the tests passed.
- **Done, host-only F0b:** `components/orcsdr_radio_discovery/include/orcsdr_radio_discovery/library.hpp` implements separate bounded RF/directory/user records, conservative identity matching and an explicit little-endian two-slot CRC snapshot journal. Simulated corruption/torn-write recovery and Linux POSIX durable-file restart tests pass in Debug, Release and ASan/UBSan builds. [Evidence and limits](F0B_HOST_VALIDATION_2026-11-11.md).
- **Not done:** ESP32-P4 SD adapter/power-loss qualification, P25 v2 migration, scanner or tuner wiring, on-device memory testing, DMR on-device decoding or any NXDN decoder, real-air RF proof, provider integration. **F0 overall remains incomplete.**
- **Delivered F0c source, host-only:** `components/orcsdr_radio_discovery/esp_idf/` provides an opt-in microSD `SlotIO`, two dedicated filenames and host fault tests. The author reports host testing; no independent current-branch CI, ESP-IDF compile, hardware SD or power-loss acceptance is established.
- **Delivered D1-A and D1-B source, independently:** frontend IQ→dibits and protocol dibits→validated LC events, with their own synthetic tests.
- **D1-C synthetic end-to-end verified:** `components/orcsdr_dmr/d1_integration/**` connects the existing modules without inventing missing acquisition bits. [Release + ASan/UBSan CI run](https://github.com/hardcoreerik/OrcSDR/actions/runs/43156539511) both pass using real D0/D1 sources; a nine-burst synthetic BS data stream yielded **eight complete decoded bursts and eight integrity-validated radio IDs/talkgroups**, correct DMR color code and alternating TS1/TS2. The **first acquisition burst is deliberately missed**; short calls and real-air recordings are not accepted yet.
- **Independent protocol tests now executed:** [GitHub Actions 43158073956](https://github.com/hardcoreerik/OrcSDR/actions/runs/43158073956) compiled the actual D0/D1 code and passed **five CTest cases** in Release and sanitizer builds. **13 ETSI reference, 16 replay harness and 38 P25 migration-preview Python tests passed**. The conformance suite reproduced **D1-CACH-01**, a real slot-attribution confidence defect; test passes do not mean that defect was fixed. Rendered ETSI analysis resolved the Golay table text-extraction ambiguity without changing decoder constants.
- **D1 replay executable implemented and host tested:** `components/orcsdr_dmr/d1_integration/tools/d1_replay_bridge.cpp` reads CF32 LE 48 kS/s from stdin and emits strict JSONL v1 for `tests/dmr_replay_acceptance/acceptance.py run`. [Release + ASan CI run 43158609068](https://github.com/hardcoreerik/OrcSDR/actions/runs/43158609068) passed both integrated synthetic IQ and CLI negative/EOF tests (2/2 CTests in each job). It emits **unknown** rather than misleading BS CACH-derived slot attribution without validated cadence. **Private real-air IQ has NOT been replayed; first-burst loss remains.**
- **P25 v2 migration preview delivered:** `tools/p25_discovery_migration/**` parses user profiles read-only, reports conflicts and preserves settings in sidecar JSON. `plan.can_apply=false` because the shared journal cannot yet faithfully represent hold, scan, talkgroup alias and receiver control fields. No P25 profile is modified.
- **Next three parallel workstreams (planned, not complete):** [D1-R recorded-IQ verification](NEXT_PHASE_D1_ACCEPTANCE.md), D1-P bounded pre-roll for real first-burst recovery, and D1-S CACH provenance/cadence correction. Exclusive folders and exact exit criteria are in the [next-phase handoff](NEXT_PHASE_D1_ACCEPTANCE.md). Lead retains `d1_integration/**`, CI reconciliation, and this tracker. **Do not claim real-air acceptance until pinned IQ is actually replayed.**
- **Earlier sandbox deliveries accepted:** D0 sync/CACH/BPTC, host-only CSV/JSON import preview, multi-protocol replay manifest, D1-A/B frontend/protocol, F0c host adapter, conformance oracles, P25 migration preview, and D1 replay acceptance harness. The [next three agents](NEXT_PHASE_D1_ACCEPTANCE.md) must not overwrite each other's directories.

## DX / user-facing priority matrix (owner-approved, 2026-11-11)

| ID | Feature | Priority | Why it matters | Status | Minimum acceptance evidence |
|---|---|---|---|---|---|
| DX-01 | Discover unknown DMR frequencies, color codes and slots | **P1** | Removes manual configuration | Not started | Discover an unknown supported candidate without a supplied CC/TG; prove mode, CC and slot using valid on-air or replayed frames; energy-only candidates remain unknown |
| DX-02 | Weak-signal diagnostics and frequency-offset display | **P1** | Real benefit for DX reception | Not started | Label signal levels as relative unless calibrated; host vectors exercise offset/noise; show CRC/FEC/sync and sample-discontinuity counters |
| DX-03 | Both time slots monitored simultaneously | **P1** | Avoids missing half the channel | Not started | From one 12.5 kHz DMR carrier, correct TS1/TS2 association under concurrent synthetic and recorded traffic; no requirement for dual tuner or simultaneous different-frequency following |
| DX-04 | Last-heard and first-heard history | **P1** | Turns casual monitoring into a useful log | Not started | Persist timestamps, receive frequency, identity, evidence level and encounter count on SD; survive reboot, incomplete write and clock-unknown case |
| DX-05 | Offline radio ID/callsign lookup | **P2** | Identifies callers without internet | Not started | Match against explicitly installed, rights-cleared and version-pinned local index; unknown IDs remain unknown; decoded data and lookup text labeled separately |
| DX-06 | Automatic repeater candidate matching | **P2** | Connects live signals to local directory data | Not started | Rank nearby entries by protocol/frequency/IDs when available; result labeled CANDIDATE until adequate RF/site identity validates it; mismatches stay visible |
| DX-07 | IQ recording and deterministic replay | **P2** | Lets users revisit difficult receptions | Not started | Save encoding/sample rate/tune/gain/discontinuity/time provenance and checksum; deterministic host replay matches expected event output, records drop metrics |
| DX-08 | Reception heatmap and antenna comparison | **P3** | Valuable for antenna testing and long-term DX analysis | Not started | Compare same-channel relative metrics over location/time and antenna sessions; identify receiver/gain calibration context; never invent geolocation |
| DX-09 | Cross-protocol discovery (P25/DMR/NXDN) | **P3** | Makes a real digital-radio search engine | Not started | Per-protocol evidence-backed recognition with false-positive and no-signal tests; transparent single-tuner scan limitations |
| DX-10 | Tier III and proprietary DMR system tracking | **P4** | Expands commercial-system monitoring | Not started | Each standard/vendor family has documented compatibility, host protocol tests and site/grant validation; no unsupported blanket trunking claim |

## Migration and profile enrichment track

The user specifically wants simple import and migration of existing radio data/accounts. This is a **product requirement**, not an optional afterthought.

| ID | Feature | Priority | Status | Acceptance gate |
|---|---|---|---|---|
| CFG-01 | Offline `Bring Your Radio Setup` file importer (OrcSDR profiles, portable CSV, documented OrcSDR JSON) | **P1** | Host preview delivered; apply/UI pending | Import via SD or authenticated LAN, preview/validate before atomic save, per-row failures, undo/rollback, zero internet dependency |
| CFG-02 | Safe migration of P25 schema-v2 profiles into shared radio library | **P1** | Read-only migration preview delivered; lossless apply/sidecar pending | Preserve current 16-profile/8-channel/8-alias behavior; detect conflicts and never truncate silently; existing profiles work unmodified |
| CFG-03 | Import review: duplicate detection, conflict resolution, mapping, selective merge | **P1** | Host preview delivered; on-device review pending | Content-based identity (not filename), preserve separate sites sharing RF frequency; show unsupported fields and allow keep-both/skip |
| CFG-04 | Offline regional directory packs and manual user import | **P2** | Not started | Provenance/terms cleared; indexed locally by location; signed/correct hash when delivered through catalog; continue without connectivity |
| CFG-05 | Optional RepeaterBook/RadioReference/RadioID/BrandMeister/FCC adapters | **P2/P3** | Not started | Verify current API/commercial/redistribution/cache/subscription rights separately for each provider before build; opt-in download/refresh; no silent upload of user location |
| CFG-06 | Account migration / connected import where provider allows | **P3** | Not started | Explicit user authorization; secure supported authentication; import preview; local snapshot usable offline; disconnect and remove imported cache; no password storage |
| CFG-07 | Backup/export/restore and provider-specific CSV mappings (e.g., tested CHIRP exports) | **P2** | Not started | Versioned schema, reversible export, documented fields, no claim of universal codeplug support |
| CFG-08 | One-tap save after real discovery; user labels separate from RF observations | **P1** | Not started | A signal observation cannot overwrite directory source facts or user's annotations; support unconfirmed candidate vs verified decode |

**Provider restrictions:** A directory listing, FCC license, RadioID record or internet-sourced alias is not an RF observation. Current documented [DATA_SOURCE_LEDGER.md](../DATA_SOURCE_LEDGER.md) rules for redistribution, source URL, retrieval, transformation, copyright/terms and rollback remain mandatory. API access must never be treated as implied permission for offline redistribution or commercial appliance use.

## System foundations and delivery order

| Gate | Work | Status | Evidence required to close |
|---|---|---|---|
| F0 | Common event/profile schema with proof levels, receiver-location semantics and bounded SD index; design cross-protocol API | **F0a/F0b host tested; F0c adapter committed (host only); device SD and migration pending** | Versioned schema, host tests for provenance, malformed imports, dedupe, time-unknown, migration/rollback; P25 behavior preserved |
| D0 | DMR constants/oracle | **ETSI constants independently host-validated; 13 Python reference tests and five production-linked CTests passed in Release/ASan; D1-CACH-01 remains a D1 framing issue** | Independently generated ETSI sync/CACH/FEC vectors; cross-check published RSA/REA constant conformance; host tests pass |
| D1 | DMR Tier II metadata decoder | **Synthetic IQ→metadata and strict replay CLI host CI passed. D1-R real-air, D1-P first burst, D1-S slot provenance and device performance NOT ACCEPTED** | Host synthetic IQ tests for 4FSK, sync, dual-slot CACH, slot type, FEC, group/source IDs, direct/repeater and late entry; measured memory/alloc bounds |
| D1-R | Pinned private DMR real-IQ acceptance + noise/AM negatives | **ASSIGNED next; NOT RUN on real recorded IQ** | Verify source hashes; direct-mode source/group/CC and integrity, multiple chunks, no false identity; separate upstream claims from OrcSDR measurements |
| D1-P | Fixed-memory pre-roll and complete first-burst recovery | **ASSIGNED next; first acquisition burst currently lost** | Complete single-burst synthetic IQ metadata and correct timestamps without fabricated payload; bounded RAM and lock/discontinuity tests |
| D1-S | Confirm BS CACH cadence before slot promotion | **ASSIGNED next; D1-CACH-01 reproduced in CI** | Random/zero-prefix rejects confidence upgrade; continuous BS cadence permits supported slot; direct-mode slot unchanged; identity integrity remains independent |
| D2 | DX-01 to DX-04 plus offline logbook and CFG-01/02/03/08 | Not started | Single-tuner lifecycle, scan/candidate/save flows, reboot and SD failure tests, no-net integration, P25 regression |
| D3 | Pack imports and offline ID/candidate matching (DX-05/06), IQ recorder/replay (DX-07) | Not started | Authorized data/terms, deterministic fixture and corruption tests, replay without SDR hardware |
| D4 | NXDN48/NXDN96 independent protocol cores on shared verified DSP primitives | Not started | Replay/test fixture gates for LICH/SACCH/RAN/identity, distinguish NXDN modes, no P25/DMR regression |
| D5 | Cross-protocol hunt, antenna comparison and map overlays (DX-08/09) | Not started | Measured sensitivity/false-positive/CPU + memory with noisy/quiet/simultaneous systems, offline map acceptance |
| D6 | DMR Tier III and named vendor systems (DX-10), plus voice work | Not started | Per-family spec/compatibility and replay/live RF gates; voice requires independent AMBE+2 feasibility/licensing decision |

P1 must not claim a working DX discovery feature before a real DMR decoder successfully distinguishes energy from valid frames. F0/D0 may progress in parallel in separate testable modules, but a new shared DSP abstraction should be promoted only after it serves two independently verified consumers. DMR decoding comes first; do not block DMR progress on NXDN/Phase II implementation.

## Next execution order and gate ownership

1. **Parallel, no folder overlap:** D1-R agent writes only `tests/dmr_replay_acceptance/**`; D1-P agent writes only `components/orcsdr_dmr/d1_frontend/**`; D1-S agent writes only `components/orcsdr_dmr/d1_protocol/**`. All may read D0 and bridge sources. No agent edits `docs/dmr/**`, D1-C integration, the other two owned directories, firmware or shared CMake.
2. **Lead integration follows:** after each agent reports commit SHA and exact test results, inspect and combine the additive pre-roll and slot evidence APIs into `components/orcsdr_dmr/d1_integration/**`; re-run CI and the recorded-IQ harness. Preserve evidence-level distinctions and existing validated identity checks.
3. **Device separate:** F0c microSD adapter still needs physical ESP32-P4 SD/power interruption evidence, internal RAM/PSRAM measurements and non-destructive P25 compatibility; D2 scan/UI remains gated by accepted host decoder behavior. **Dashboard design remains deferred.**

See [NEXT_PHASE_D1_ACCEPTANCE.md](NEXT_PHASE_D1_ACCEPTANCE.md) for stop/acceptance criteria and exact commands. **Latest phase documentation is not a claim of a completed decoder or verified real-air capture.**

## Non-negotiable architecture and safety rules

- Preserve currently verified P25 Phase I clear voice and the existing partial P25 Phase II work; no backward movement in dashboard IDs or persistent profiles.
- Separate P25, DMR and NXDN framing/state/FEC details. Share reusable channelization/DSP primitives, radio-session ownership, scanner infrastructure, event schema and bounded storage where appropriate. No protocol-copy shortcuts.
- Avoid `main.cpp` growth; protect memory, task scheduling, DMA reserve and tuner lifecycle. Any new CPU/RAM budget is a proposed gate until measured on ESP32-P4.
- Record distinct **source/measurement states**: `directory_candidate`, `signal_observed`, `protocol_signature`, `crc_valid_decode`, `site_confirmed`, `user_annotation`. The display must never silently upgrade evidence confidence.
- Keep network access optional, secure and explicitly user initiated. User-set location is not derived from received identity, and client/account fields are not automatically uploaded.
- New public packs only after redistribution terms, signed catalog validation and removal process are defined. User-importable local files may be subject to separate license/terms rules.
- No guarantee of decoding encrypted speech. Distinguish network talkgroup routing from long-distance RF.
- Use read-only RSA/REA research evidence; do not copy competitor source or assume measured RF performance from code structure alone.
- Stage implementation separately from research; all tests, firmware builds and physical-device proof are distinct acceptance levels.

## Developer handoff / evidence

1. [Five-application DMR RSA/REA comparison](https://github.com/hardcoreerik/REA-Results/tree/research/dmr-competitor-20261111) (private, unmerged): SDRTrunk, SDRangel/DSDcc, DSD-FME, OpenWebRX+/digiham, DSDPlus. Static metrics and protocol constant derivation exist; **no comparative IQ playback, voice decode or device benchmarks were run**.
2. [Older digital-voice research fixtures](https://github.com/hardcoreerik/REA-Results/tree/chatgpt/digital-voice-rsa-rea-2026-11-01/inbox/fixtures/digital-voice) (private, unmerged): contains recorded IQ assets for DMR, P25 Phase I and NXDN48/96; fixture provenance and per-format conversion are required before test claims.
3. [Current P25 profile format](../RADIO_CONFIGURATION.md), [signed data catalog](../DATA_CATALOG.md), [data-source terms ledger](../DATA_SOURCE_LEDGER.md) and [P25 Phase II evidence](../P25_PHASE2_WORKPHASE.md).

## Progress update protocol

When closing a row, add the implementing PR/commit, source/binary pin, exact test names, test environment, RF/host evidence class, CPU/peak RAM/storage measurements (or `NOT MEASURED`) and any uncovered modes. Update this tracker only for status supported by evidence. No unreviewed provider or production feature should be marked delivered from vision text alone.
