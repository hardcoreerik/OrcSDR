# OrcSDR NXDN — approved market strategy and receive-only development roadmap

**Decision date:** 2026-11-22  
**Decision authority:** Project owner, explicitly selected in the NXDN market research conversation  
**Canonical development branch:** `feature/dmr-digital-voice-foundation`  
**Status:** APPROVED PRODUCT PRIORITIES / PLANNED ENGINEERING — **not implemented**  
**Research workspace:** `hardcoreerik/REA-Results`, separate research artifacts; reviewed conclusions are captured here on the canonical OrcSDR branch

## 1. Three binding product decisions

| Decision | Approved direction | Implication |
|---|---|---|
| Primary customer | **BOTH** amateur/scanner/DX enthusiasts **and** professional/commercial radio technicians, integrators and field analysts | One core receiver and evidence system, with distinct user experiences; do not force commercial users into amateur-radio assumptions or vice versa |
| NXDN trunking | **BOTH Type-C and Type-D** | Independently design, implement, replay-test and field-qualify each architecture; neither is implicitly covered by the other |
| NXDN voice | **EARLY, in parallel with decoder development** | Start AMBE+2 licensing/technical due diligence and receive-only voice-path feasibility during the first engineering phase; do not wait until trunking is finished |

These are planning commitments, **not** claims that NXDN48, NXDN96, Type-C, Type-D or voice are currently working in OrcSDR.

**Product promise:** Discover unfamiliar NXDN activity without a prior codeplug; understand RAN, radio/talkgroup identities and channel behavior when justified by validated signaling; measure RF quality; save/export locally; optionally listen to permitted clear voice when a qualified lawful vocoder path exists.

Keep amateur-radio users a first-class design audience while providing credible tools for commercial system survey and diagnostics. The default experience should be offline-first, receive-only, consent-driven and evidence-qualified.

## 2. Market findings: user problem, product response, validation

This is a **qualitative opportunity assessment**, not a measured market-size estimate or proof of demand. Product and competitor capability claims must be version-pinned before implementation commitments.

| Observed/indicated need | User persona | OrcSDR response | How to validate the opportunity |
|---|---|---|---|
| NXDN is not identified automatically by every scan workflow; setup is cumbersome | Amateur, scanner, newcomer | Auto-recognize NXDN48/96, show RAN/protocol evidence; one-tap receive-profile preview | Interview and usability-test operators discovering unfamiliar local systems |
| Operator wants to know *who*, *where*, and *when* traffic occurred | Amateur/DX, field technician | Last Heard, first heard, call history, frequency/RAN/source/destination provenance, optional aliases | Measure completion of real tasks and errors with offline-only UI |
| Trunked systems are difficult to understand and follow | Scanner, technician | Site/channel/grant timeline, independent Type-C/Type-D views, call and control history | Verify against independent Type-C/Type-D IQ and known channel plan |
| Portable coverage and antenna comparisons lack evidence-rich logging | DX, installation, service | Relative RF quality, CFO, acquisition, CRC/FEC, lost frames, time/location context, export | Run controlled RF/antenna comparisons with repeatability checks |
| GPS, text, status and data are often harder to search/use than raw logs | Fleet researcher, technician, hobbyist | Structured validated position/message event records, no inferred location | Ground-truth position/message fixtures and privacy review |
| People expect to *hear* clear NXDN, not just view metadata | Both markets | Separate lawful receive-voice program, metadata-first UI with voice readiness shown honestly | Codec feasibility, licensing review and real-air intelligibility tests |
| Imported frequency lists can be stale, or no frequency map exists | Both markets | Reverse-codeplug discovery; learned candidate channel mappings; CSV/JSON import preview | Test ambiguous or missing maps, duplicate sites and retune failure cases |

**Commercial boundary:** OrcSDR is an independent **RF field analyzer**, not a replacement for an authorized NEXEDGE/IDAS dispatch console, provisioning server, or repeater-management interface. Passive RF reception cannot imply remote radio control, registration, authenticated dispatch, over-the-air reprogramming or equipment management. Do not build TX/Pluto dependencies into this RX roadmap.

## 3. Product concept: Discover → Understand → Measure → Remember

### Discovery / Last Heard (cross-protocol foundation)
- Find candidate NXDN signals on a user-selected frequency or documented scan plan, distinguish *energy*, *plausible signature*, *integrity-validated frame* and *validated identity*.
- Identify NXDN48 vs NXDN96 only after the corresponding independent gates pass. Display RAN and protocol-local identifiers only when decoded and protected by the appropriate checks.
- Build **Last Heard** and system activity timelines: RF frequency, available RAN/system/site, source, destination/group, observed call type, first/last seen, evidence level, sample epoch and loss counters.
- Preserve unknown fields as **unknown**, not fabricated values. Preserve lookup aliases, user notes and observed identifiers with separate origins.
- Create a **receive-profile preview** from observations; no automatic destructive codeplug programming. Import and export offline with versioned provenance.

### Professional NXDN Field Analyzer
- Carrier/channel utilization, verified grants, channel map, site activity, known RANs and traffic types.
- Signal-quality history: relative level unless calibrated, estimated SNR with method, CFO, timing error, FEC corrections, CRC success, lock/relock, truncation and discontinuity counters.
- Survey sessions with **receiver** location context and explicit equipment/gain/calibration; GPS reported by a radio is a separate *decoded data observation*, not the receiver position or inferred site coordinates.
- KML/CSV/JSON and reports only when provenance permits; raw IQ replay should be optional and storage-bounded.
- Field-oriented diagnostics should make incorrect attribution visibly impossible or explicitly uncertain.

### Type-C and Type-D trunking — both required tracks
- **Type-C:** dedicated control-channel acquisition, system/site identification, grants, traffic-channel mappings, channel/frequency plan, call correlation, receiver switching constraints.
- **Type-D:** independently verify distributed control and home-channel/repeater signaling, SCCH, channel assignments and call transitions; **do not** assume Type-C logic works without a continuous control channel.
- Each needs independent synthetic *and genuine over-the-air* fixtures, noise/false-identity controls, lost-grant/reacquisition, channel-map gaps, multi-site ambiguity and appropriate per-receiver limitations.
- Single receiver cannot follow two separated channels simultaneously outside its IQ bandwidth. Never claim unrestricted multi-channel following or simultaneous call recording without hardware and DSP proof.

### Early voice-decoding parallel track (V0, starts alongside R0/H0)
- Investigate DVSI AMBE+2 software and hardware options, relevant rights, terms, commercial distribution, costs, libraries, cross-platform support and ESP32-P4 memory/CPU/latency. Evaluate alternative interoperable interfaces legally and technically.
- Prepare bounded receive-only **clear voice** plumbing proposal with strict privacy/encryption handling, frame integrity, session lifecycle, audio mute/resume, errors and drop counters.
- No speech decoding of encrypted/privately protected traffic; show encrypted/unknown state without falsely presenting intelligible audio.
- Implementation must remain gated until a technically validated and commercially appropriate vocoder route is selected. Early means **start research and independent feasibility now**, not promise shipping voice before the rights and hardware are known.
- Voice is a separate capability and must not block protocol metadata acquisition or imply Pluto/TX support.

## 4. Engineering roadmap: explicit dependencies and gates

| Track | Priority | Deliverable / hard acceptance | Status |
|---|---|---|---|
| **NX-R0** standards and competitor/market research | P0 | Exact NXDN48/96 CAI revisions, common and mode-specific FSW/LICH/SACCH/FACCH/CAC/SCCH, Type-C/Type-D layouts; license/source register; recover source-pinned real-IQ candidates; user/technician interviews | Planned |
| **NX-V0** voice feasibility / licensing | P0 **parallel** | Document software and hardware AMBE+2 candidates; legal/commercial feasibility, P4 resources, audio pipeline requirements; explicit go/no-go decision | Planned |
| **NX-H48** NXDN48 host metadata core | P1 | Standalone portable component, independent frame sync, FEC/CRC, supported RAN/identities, synthetic negative controls, Release + ASan/UBSan, no heap where feasible | Planned |
| **NX-H96** NXDN96 host metadata core | P1 | Independent verified 96-mode framing/FEC/CRC/RAN/identities, mode-negative controls, separate host evidence | Planned |
| **NX-IQ** IQ frontend and real-air acceptance | P1 | Exact 48/96 parameters verified against authoritative standards, IQ → hard/soft symbols, CFO/timing/polarity, source-hashed real-air positives and cross-mode negatives, impairment/chunk determinism | Planned |
| **NX-DX** Discovery / Last Heard | P1 | Typed events, source-evidence gating, RAN/ID unknown states, frequency/user profile preview, bounded offline log and no-internet test | Planned |
| **NX-C** Type-C control/trunking | P1/P2 | Control-channel parsing, independently validated channel grants, correct site/frequency map, replay and field acceptance | Planned |
| **NX-D** Type-D distributed trunking | P1/P2 | Independent SCCH/home-repeater mapping and grant/call correlation; real Type-D capture gate, no inheritance of Type-C success | Planned |
| **NX-DATA** structured data | P2 | Validate supported position, text, and status formats with independent positive/negative fixtures; provenance and user privacy rules | Planned |
| **NX-P4** ESP32-P4 qualification | P1 after host gates | Cross-compile, instrument CPU/heap/stack/PSRAM, bounded read-only IQ tap through existing owner, loss statistics, P25/DMR regression and actual device measurements | Planned |
| **NX-UI** field analyzer / operator view | P2 after data contracts | Discovery history, activity map/atlas, separate Type-C/Type-D views, audio state, export; integrate through refactored adapter, no decoder in main.cpp | Planned |
| **NX-VOICE** clear voice reception | P1, **dependent on NX-V0 go** | Validated clear NXDN48/96 voice path, codec licensing satisfied, sync-loss and encrypted-call negative tests, device performance and audio latency acceptance | Blocked on qualification |

**Parallel execution:** NX-R0 and NX-V0 should begin immediately and independently. NX-H48 and NX-H96 may share proven DSP interfaces, but each must satisfy its own protocol acceptance. Type-C and Type-D should both enter design/test planning in R0 and receive separate development tracks once foundational IQ/metadata evidence is sound; **neither may be deferred silently**. Discovery event contracts may be designed alongside the host cores; live scanning and UI must wait for receiver-owned IQ integration.

**Minimum first demonstration:** Receive genuine source-hashed NXDN48 and NXDN96 IQ, recover at least one protocol-specific integrity-validated event/RAN per mode, reject cross-protocol false identities, log truthful discoveries, and generate a receive-profile preview. This does **not** prove voice or trunk following.

**Product-level acceptance:** Both mode variants; independently qualified Type-C and Type-D; separately cleared and working voice when legally feasible; offline-first discovery; reliable replay, provenance, bounded resource use, validated P4 reception and no regressions to existing P25/DMR.

## 5. Repo ownership, product boundaries, integration

- **Single canonical OrcSDR development branch:** `feature/dmr-digital-voice-foundation`. NXDN implementation, if subsequently authorized, is expected in a dedicated `components/orcsdr_nxdn/**` component with standalone test targets. No second permanent NXDN foundation branch.
- **Research goes to REA-Results**; the approved decisions and product roadmap are **also** committed here, to prevent findings from being orphaned outside the main Digital Voice effort. Research findings require reviewed handoff before being treated as specifications or decoder acceptance.
- **No `main.cpp` DSP or protocol code**. The concurrent `claude/orcsdr-main-cpp-refactor-dcac06` branch is read-only; use a separately reviewed small adapter once stable.
- **No duplicate USB/SDR owner**, no concurrent tuner retune, no assumption of free simultaneous receiver bandwidth; 48 kS/s channel IQ compatibility for both modes must be verified.
- **No Pluto / TX** work in this plan. Preserve existing P25, DMR and D-STAR/YSF test suites, storage schema, dashboard IDs and radio-owner behavior.
- Do not merge to `main` or force-push. Short-lived task branches must originate from and return to the canonical branch.
- Existing DMR host D1-N 40/40 real-IQ acceptance is complete; that result does **not** validate any NXDN capability.
- No NXDN production decoder, source-pinned NXDN mode acceptance, Type-C/Type-D tracking or voice path is currently claimed.

## 6. Benchmark and evidence register for follow-up research

These are **starting points to verify and version-pin**, not permission to copy their code or a claim of feature parity:

- NXDN Forum official standards portal: https://www.nxdn-forum.com/download_file/
- NXDN Forum overview: https://www.nxdn-forum.com/what-is-nxdn/nxdn-a-brief-overview/
- JVCKENWOOD NEXEDGE: https://comms.kenwood.com/
- Icom IDAS: https://www.icomjapan.com/
- DSD-FME: https://github.com/lwvmobile/dsd-fme
- DSD-neo: https://github.com/arancormonk/dsd-neo
- SDRTrunk: https://github.com/DSheirer/sdrtrunk
- GopherTrunk: https://github.com/MattCheramie/GopherTrunk
- RadioReference community forums: https://forums.radioreference.com/
- DVSI vocoder options: https://www.dvsinc.com/

**Evidence status:** The earlier user-demand and competitor feature assessment is qualitative and may be stale; it is **not** a paid-product sales forecast or verified count of NXDN customers. For each investigated feature, attach dated original issue/feature request URL, user scenario, source type, release/commit, protocol mode, and an acceptance test or user interview. Avoid inferring market share from forum volume.

**Adoption / success metrics to define before product signoff:** time to first valid NXDN discovery with no profile; percentage of replayed true NXDN48/96 events recovered; false radio-ID/site assertions in independent negative corpus; number of taps required to save an observed channel; last-heard correctness across discontinuities; genuine Type-C/D follow success on supported hardware; voice intelligibility/latency once licensed; P4 CPU/heap/PSRAM/headroom; local log recovery after power interruption. Record measured numerator, denominator, environment and source hashes.

## 7. Next approved planning handoffs (implementation not authorized by this document)

1. **Research agent (REA-Results):** establish two protocol maps, source-pinned IQ inventory, competing claims and Type-C/Type-D field-capture gaps. Return source register and feasibility report.
2. **Voice/licensing analyst (REA-Results or separate scoped research):** obtain actual supplier terms, P4 cross-platform estimates, codec integration alternatives and a lawful, documented decision gate.
3. **OrcSDR lead:** review both outputs; issue separately owned host NXDN48, NXDN96 and independent conformance tasks targeting the same Digital Voice branch. Require measured CI and evidence review before claiming implementation.

*Record of intent only. The user's decisions are approved; none of the listed NXDN engineering gates are marked complete by this document.*
