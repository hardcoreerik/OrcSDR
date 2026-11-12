# DMR / Digital Voice — development tracker

**Status:** F0a evidence contract and F0b host storage implemented (not firmware-integrated); native DMR and NXDN decoders remain planned. **Created:** 2026-11-11.
**Product requirements:** [VISION.md](VISION.md). **Current implemented truth:** [PROJECT_STATUS.md](../../PROJECT_STATUS.md).
**Evidence:** [DMR RSA/REA research branch](https://github.com/hardcoreerik/REA-Results/tree/research/dmr-competitor-20261111), plus the older [digital-voice fixture branch](https://github.com/hardcoreerik/REA-Results/tree/chatgpt/digital-voice-rsa-rea-2026-11-01). Both are research artifacts, not implementation acceptance.

> Priority is not completion status. Every item below is **NOT STARTED** unless explicitly marked otherwise. An imported directory hit is never a verified RF decode. No network connection is needed for the promised core workflows.

## Active implementation checkpoint — F0b host storage (2026-11-11)

**Integration branch:** [`feature/dmr-digital-voice-foundation`](https://github.com/hardcoreerik/OrcSDR/tree/feature/dmr-digital-voice-foundation).
[Parallel workstreams and restart instructions](WORKSTREAMS.md) set exclusive folder ownership for additional AI sessions.

- **Done, host-only:** `components/orcsdr_radio_discovery/` contains the header-only typed `Observation` contract (origin, proof level, protocol identity with known-field flags, receiver location, relative measurement fields) and a bounded `ObservationRing`. Tested locally using CMake/CTest, a Release build with `NDEBUG`, and AddressSanitizer/UndefinedBehaviorSanitizer; the tests passed.
- **Done, host-only F0b:** `components/orcsdr_radio_discovery/include/orcsdr_radio_discovery/library.hpp` implements separate bounded RF/directory/user records, conservative identity matching and an explicit little-endian two-slot CRC snapshot journal. Simulated corruption/torn-write recovery and Linux POSIX durable-file restart tests pass in Debug, Release and ASan/UBSan builds. [Evidence and limits](F0B_HOST_VALIDATION_2026-11-11.md).\n- **Not done:** ESP32-P4 SD adapter/power-loss qualification, P25 v2 migration, scanner or tuner wiring, on-device memory testing, native DMR or NXDN decoding, RF proof, provider integration. **F0 overall remains incomplete.**
- **Next lead checkpoint F0c:** ESP32-P4 SD `SlotIO` integration, flash/SD interruption and recovery evidence, plus non-destructive P25 profile migration compatibility tests. Preserve P25 v2 profiles and keep directory/user data distinct from RF observations.
- **Parallel sandbox gates:** D0 DMR constants, host-only offline import preview, and pinned IQ/replay manifest; no overlapping file edits.

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
| CFG-02 | Safe migration of P25 schema-v2 profiles into shared radio library | **P1** | Not started | Preserve current 16-profile/8-channel/8-alias behavior; detect conflicts and never truncate silently; existing profiles work unmodified |
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
| F0 | Common event/profile schema with proof levels, receiver-location semantics and bounded SD index; design cross-protocol API | **In progress: F0a+F0b host validated; device SD and migration pending** | Versioned schema, host tests for provenance, malformed imports, dedupe, time-unknown, migration/rollback; P25 behavior preserved |
| D0 | DMR constants/oracle | **Host source/tests committed; independent gate pending** | Independently generated ETSI sync/CACH/FEC vectors; cross-check published RSA/REA constant conformance; host tests pass |
| D1 | DMR Tier II metadata decoder | Not started | Host synthetic IQ tests for 4FSK, sync, dual-slot CACH, slot type, FEC, group/source IDs, direct/repeater and late entry; measured memory/alloc bounds |
| D2 | DX-01 to DX-04 plus offline logbook and CFG-01/02/03/08 | Not started | Single-tuner lifecycle, scan/candidate/save flows, reboot and SD failure tests, no-net integration, P25 regression |
| D3 | Pack imports and offline ID/candidate matching (DX-05/06), IQ recorder/replay (DX-07) | Not started | Authorized data/terms, deterministic fixture and corruption tests, replay without SDR hardware |
| D4 | NXDN48/NXDN96 independent protocol cores on shared verified DSP primitives | Not started | Replay/test fixture gates for LICH/SACCH/RAN/identity, distinguish NXDN modes, no P25/DMR regression |
| D5 | Cross-protocol hunt, antenna comparison and map overlays (DX-08/09) | Not started | Measured sensitivity/false-positive/CPU + memory with noisy/quiet/simultaneous systems, offline map acceptance |
| D6 | DMR Tier III and named vendor systems (DX-10), plus voice work | Not started | Per-family spec/compatibility and replay/live RF gates; voice requires independent AMBE+2 feasibility/licensing decision |

P1 must not claim a working DX discovery feature before a real DMR decoder successfully distinguishes energy from valid frames. F0/D0 may progress in parallel in separate testable modules, but a new shared DSP abstraction should be promoted only after it serves two independently verified consumers. DMR decoding comes first; do not block DMR progress on NXDN/Phase II implementation.

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
