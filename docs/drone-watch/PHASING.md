# Drone Watch — phased implementation plan

> **W01 implementation update (2026-10-27):** Live Watch shell/source-status model is committed and host-tested on this branch, but no Remote ID decoder, C6 capture adapter, firmware build or physical device test is completed. Begin remaining phases with a verified decoder/model and acquisition gate. Every phase is a separately buildable, independently reversible unit. Never treat compilation as a real-air acceptance test.

**25 per-screen implementation specs:** [SCREENS.md](SCREENS.md), with shared UI/UX and rendering rules. These are future target contracts, not accepted runtime capabilities. Initial P3 covers only the smallest truthful screens; the remaining specifications become feature-gated incrementally.

**Design:** [DESIGN.md](DESIGN.md)  
**Acceptance:** [VALIDATION.md](VALIDATION.md)  
**Developer handoff:** [HANDOFF.md](HANDOFF.md)  
**Research:** private [REA-Results PR #3](https://github.com/hardcoreerik/REA-Results/pull/3), October 26, 2026, including upstream source/commit/permission ledger.  
**Source baseline inspected:** OrcSDR `main` at `07c777ca05af4bcbf8af6074dbc0b17d9a891af1`; rebase and recheck on implementation day.

## Global constraints

- **Receiver first, UI second.** No banner, map marker or 'detected aircraft' from unsupported or synthetic radio observations.
- **Passive-only:** no drone control packets, interference, jamming or bypass of protected links.
- **C6 ownership:** keep P4/C6 ESP-Hosted, Wi-Fi, SD card and existing firmware-recovery semantics intact. Initial pass is capability discovery + recordings on a spare device. No auto-flash of the C6.
- **Receiver ownership:** RTL-SDR already has a single owner and pinned driver version. No second USB lifecycle or duplicated IQ/DSP stack.
- **Frame ownership:** use existing ScreenController; no drawing in DSP, USB, Wi-Fi callbacks or SD-worker tasks. Preserve existing common dashboard header.
- **Evidence separation:** architecture/source review, synthetic parser tests, host model regression, native build, exact binary flash, observed C6 packets and live Remote ID RF are independent gates.
- **Adaptive UI:** responsive model/renderer with 1280×720 and 720×720 acceptance; do not fork complete screens by physical display.
- **All read-only synthetic/demo data must visibly say TEST DATA.** Real device scanning state must be reported by acquisition sources rather than hard-coded.
- Each phase gets `PASS | FAIL | BLOCKED | NOT RUN` evidence and can be disabled/rolled back without breaking baseline FM/ADS-B/LoRa/Wi-Fi/Dead Air.
- No changes to `esp-rtl-sdr`, `OrcMaps`, C6 firmware or `OrcNode` happen implicitly through this plan. Any external repo work needs its own branch, permission review and tests.

## Dependency / delivery map

```text
P0 research (documented)
  |
  v
P1 event contract + OpenDroneID C parser host verification
  |
  +-------------------------------+
  v                               v
P2 C6 feasibility / fallbacks     P3 replay-driven WATCH UI
  |                               |
  +---------------+---------------+
                  v
            P4 live RID + OrcMaps integration
                  |
                  v
           P5 saved evidence + OrcNode
                  |
      +-----------+-----------+
      v           v           v
P6 RF lab    P7 analog AV   P8 acoustics
      \           |           /
       +----------+----------+
                  v
         P9 product release gates
```

P2 is an **experiment** whose outcome can be `C6 not usable`; this must not block P3, because a replay source or independently validated OrcNode can feed the same schema. P6-P8 are optional and parallel; their failure must not block a Remote-ID-only product.

## P0 — Research provenance and feature contract (completed as documentation; no runtime capability)

- [x] Capture pinned public project inventory, licenses, design comparisons, limitations and source-reuse warnings in **REA-Results**.
- [x] Execute isolated synthetic evidence model (15/15 unit tests) and archive a separate findings record with hashes.
- [x] Map existing OrcSDR dashboard/Hosted/OrcMaps architecture and draft this plan.
- [ ] Review exact pinned third-party source and attribution with maintainer before vendoring; check whether core C can be used as a dependency instead.
- [ ] Choose outward product name (**working name: Drone Watch**), privacy defaults and MVP scope with owner.

**Exit:** source inventory and research/tests are auditable; feature claims are bounded; implementation branch separate from release work. **Status: design complete for proposal, product choices pending.**

## P1 — Reusable message decoder + bounded observation model (PR 1: host-only; lowest-risk code)

**Suggested files (names tentative):**
- `components/drone_remote_id/` (small official C library adapter / attribution as applicable);
- `apps/orcsdr-tab5/ui/drone_watch_model.{hpp,cpp}`;
- `tests/drone_watch/` host fixtures and test runner;
- minimal CMake integration **only if** required for deterministic host build.

- [ ] License audit official OpenDroneID C core at a pinned SHA, and record an SBOM/notice.
- [ ] Write tests first for valid BasicID, Location, System, SelfID, Auth and MessagePack input using upstream supplied fixtures or independently generated compliant examples; distinguish **unsupported** from **malformed**.
- [ ] Verify input length, message-version handling, endian handling, signed/invalid sentinel values and upper-bounded strings without heap amplification.
- [ ] Construct `Observation` and `Track` with capture provenance, local monotonic time, optional UTC, optional claimed position, transport and source health.
- [ ] Implement no-RID/uncertain/unknown-state behavior, out-of-order and duplicate-message handling, expiry/eviction, and no mixing of unrelated BLE/Wi-Fi identities.
- [ ] Test schema migration/version fields for later OrcNode use; support a reader that can be fed by synthetic messages without C6 hardware.
- [ ] Run host regression, sanitizers/fuzz corpus (if tools available) and calculate test-result hashes. No source may assert `verified operator` absent cryptographic evidence.

**Exit G1:** deterministic official reference vectors and negative cases pass; bounded memory and no crash across accepted invalid input suite; test runner reproduces on PC; *no radio access implied*. If the C core can be compiled for ESP-IDF, report build separately from RF.

**Rollback:** remove/disable unreferenced parser and model modules; baseline firmware behavior unchanged.

## P2 — C6 Hosted acquisition qualification (PR 2a: instrumentation, with separate P2b only after proof)

The onboard ESP32-C6 and its existing SDIO ESP-Hosted slave are the first preferred hardware path, **not a guaranteed receiver**.

- [ ] Pin the connected Tab5 board ID, P4 build SHA/IDF version, C6 firmware version and existing Wi-Fi feature baseline before modification.
- [ ] Audit `esp-hosted-mcu` v3.0.6 host/coprocessor configurations and the C6 variant actually installed: whether Bluetooth HCI advertisement events traverse the SDIO Hosted implementation and whether suitable ESP-IDF APIs are exposed.
- [ ] Explore passive **BLE4 advertisements** from an owned known broadcaster on a **spare** Tab5 with no C6 firmware change; collect raw bytes, timestamps, packet counts, errors, RSSI if supplied.
- [ ] Test Wi-Fi disconnected, idle, scanning, and active connection separately; record whether operations pause RTL-SDR and how coverage changes.
- [ ] Independently test BLE5 extended/coded advertisements **only if supported by exact firmware**, and Wi-Fi Remote-ID beacons / NAN **only if a verified raw-frame path exists**. Report UNSUPPORTED rather than approximating.
- [ ] Check C6/Hosted interrupt, bus, DMA/internal memory and task scheduling; ensure the existing M5 Launcher compatibility requirement is recorded.
- [ ] Decide acquisition: (A) ship C6 RX, (B) support C6 RX only in a distinct verified firmware configuration, or (C) use a standalone OrcNode/Android scanner instead.
- [ ] Any P2b C6 firmware change requires separate explicit owner approval, documented backup/reflash/recovery and exact-image hardware acceptance; never push a new C6 image as an unprompted side effect.

**Exit G2:** a captured **owned** Remote-ID BLE4 advertisement with raw capture + decoded fields, stable Hosted/Wi-Fi behavior and recorded failure cases, or an explicit `BLOCKED/UNSUPPORTED` decision with fallback specified. *No universal coexistence claim.*

**Rollback:** disable scanner/instrumentation and restore previously verified C6 state; preserve radio/Wi-Fi baseline.

## P3 — First 5-inch dashboard driven by known replay (PR 3; independent of P2)

- [ ] Integrate dashboard entry with `dashboard_registry`, ScreenController, Home/recent and shared header ownership; avoid adding logic to `main.cpp` beyond wiring.
- [ ] Add `drone_watch_app` lifecycle and `drone_watch_dashboard` snapshot renderer (WATCH, AIRCRAFT, SOURCES); MAP disabled until P4. Source statuses are visible even with no packets.
- [ ] Make detection messages precise: `REMOTE ID RECEIVED`, `NO REMOTE ID OBSERVED`, `SCANNER PAUSED`, `SOURCE UNAVAILABLE`; don't show `DRONE APPROACHING` or `AIRSPACE CLEAR`.
- [ ] Add a bounded history/list and drilldown with raw metadata, decoded field validity, source kind, transport, sample age and declared confidence basis.
- [ ] Show `TEST DATA` ribbon in replay injection builds. Production builds must have no silent synthetic traffic or dev injection.
- [ ] Render on 1280×720 and 720×720 from **one** UI implementation. Validate legibility and touch regions on a physical 5-inch Tab5 before declaring UI accepted.
- [ ] Back/Close/Home restores prior band state; no involuntary RTL retune or power transition when entering/leaving Drone Watch.
- [ ] Add focused UI host/regression tests including initial zero state, staleness, transient failures, navigation, resize/reflow, repeated open/close.

**Exit G3:** registry routing, loss/expiry UI, source state and navigation pass deterministic host checks and native build. Actual physical screen legibility requires an independent flash gate. This phase may run without a C6 receiver and must disclose that limitation.

## P4 — First real observations + OrcMaps (PR 4; requires G1 + verified acquisition source)

- [ ] Select a verified local adapter (P2 C6 or independent OrcNode). Ensure source callbacks produce bounded packets with data ownership and age semantics.
- [ ] Feed only valid decoded aircraft-reported location into the **existing shared OrcMaps service**; do not reproduce an old hand-drawn basemap or package a local-only map.
- [ ] Respect label On/Off and source availability; mark each pin `Remote ID reported / unverified` and expose last-seen age and source ID without clutter.
- [ ] Assert invalid/unknown lat/lon never produce pins; rejected later updates cannot silently persist stale pins.
- [ ] Define location consent and privacy treatment for operator/home fields separately from aircraft position.
- [ ] Add playback tests with differing reported coordinates, TTL expiry, rapid UI tab switching, missing map packs, offline/no-network, and no GPS position.
- [ ] Hardware-run actual authorized BLE/Wi-Fi Remote ID sample with visible screen and matching logged packet evidence.

**Exit G4:** map fidelity against decoded known test frames, no stale pins, validated source vs claimed aircraft separation, and live RF acceptance evidence on named receiver hardware. Label map accepted only when OrcMaps integration itself is proven on selected branch.

## P5 — Optional local session journal and OrcNode extension (separate PRs)

- [ ] Provide **opt-in** SD session capture with bounded JSONL + schema, monotonic + optional UTC, source capability state, loss/drop counters and clear retention controls.
- [ ] Follow Dead Air storage-worker pattern but audit SD concurrency/unmount limitations; no direct SD writes on hot/UI/USB tasks.
- [ ] Do not persist operator location or raw video by default; document identity/position redaction in exports and screenshots.
- [ ] Define versioned input envelope for remote OrcNode observation transport; authentication/integrity and source identity required for off-device data, with explicit `REMOTE SOURCE` label.
- [ ] Validate duplicate/source separation, clock drift, reconnect/loss state, queue saturation, SD-full/removal and power-cut recovery.
- [ ] Keep OrcNode changes in its own repo/branch and accept its exact hardware + software combinations separately.

**Exit G5:** repeatable logs with offline replay and no fabricated records; source health correctly reflects disconnected nodes; integration regression green. Optional for v1 MVP.

## P6 — Optional RTL-SDR sub-GHz activity profile (experimental, independent PR)

- [ ] Build an explicit **RF ACTIVITY** measurement mode using receiver ownership/scan primitives already available; no classifier claims without labeled real captures.
- [ ] Present tuned center, span, gain, usable bandwidth, sample gaps, noise floor/occupied power where those values are truly measured; mark scan duty cycle and unavailable bins.
- [ ] Gate scans behind audio/DSP/USB/SD and tuning-state regression; never assume `900 MHz` or `300 MHz` means Bluetooth or a drone.
- [ ] Record positive/negative reference signals from owned lab emitters before proposing any drone-related classifier; publish confusion matrix with unknowns before changing labels.
- [ ] Maintain existing RF Lab and FM/ADS-B/LoRa performance; don't bypass pinned driver to add new SDR scan ownership.

**Exit G6:** measured spectral events with correct units and gap reporting; `RF ACTIVITY` only until a separate, validated classifier exists. **Not required for v1.**

## P7 — Optional compatible analog FPV-video accessory (experimental, own hardware branch)

- [ ] Compare receiver modules (e.g. RX5808 lineage) as receive-only frontends; verify module/license, supported frequencies and output format, not advertised detection ranges.
- [ ] Prototype **receiver -> video decoder/digitizer -> frame transport -> P4 graphics** as four individually measured boundaries.
- [ ] If using USB UVC capture, separately test hub, USB Host ownership, power budget, memory, sustained frames and coexistence with the existing RTL-SDR V4; do not assume two devices work together.
- [ ] Treat receiver RSSI/sync as separate from an actual decodable video frame. Distinguish unsupported digital/proprietary/encrypted formats from analog compatibility.
- [ ] Expose `VIDEO VIEW — BAND SCAN PAUSED` whenever tuning/video consumes the only source covering that band; verify restore on exit.
- [ ] Time/frame-stamp known authorized lab video and measure renderer/frame/drop counters and power before any feature claim.

**Exit G7:** physical image actually displayed from controlled compatible analog source, correct scan coverage messaging, recoverable unplug/reconnect. Never claim all-drone video reception. **Not required for v1.**

## P8 — Optional acoustic candidates (experimental, own PR)

- [ ] Verify actual Tab5 ES7210 microphone path and its compatibility with the current audio implementation. Record microphone geometry, sample format and permission state.
- [ ] Build positive + negatives under controlled and different environmental conditions, with exact audio sample provenance and separate train/test sets.
- [ ] Baseline non-ML spectral features before considering classifier complexity; report false alarms/misses and conditions.
- [ ] Preserve personal audio privacy (no continuous recording without explicit opt-in and UI state). Microphone sound must not acquire a drone identity or location through an arbitrary mapping.
- [ ] Show only `POSSIBLE ROTOR SOUND` when a verified classifier runs; otherwise `AUDIO ACTIVITY` or `UNAVAILABLE`.

**Exit G8:** reproducible experiments, actual physical microphone measurements and bounded resource load, false-positive evidence and user consent. **Not required for v1.**

## P9 — Product readiness, regression and user documentation

- [ ] Repeat native ESP-IDF build on target with recorded SHA, dependency lock, app image SHA, partition headroom and exact C6 versions.
- [ ] Run baseline FM/RDS audio, ADS-B, LoRa, Wi-Fi scan/connect/catalog, SD, Dead Air, OrcMaps, sleep/wake and hotplug where applicable.
- [ ] Run on current supported Tab5 and later on a second ESP32-P4 board/720×720 display with Adaptive UI; don't claim hardware parity from compilation alone.
- [ ] Run low-memory, rapid tab switching, repeated open/close, no-radio, no-network, C6 error, stale/invalid packet and SD absent tests.
- [ ] Publish a feature matrix `available on your hardware` vs `experimental` vs `not supported`, with privacy/storage choices and RF range caveats.
- [ ] Do **not** retroactively set 'Hardware-Verified' or 'RF-Verified' for phases that only ran on a host or in a synthetic simulation.

**Exit G9:** same exact tagged binary survives release/regression checks on explicitly named hardware and actual appropriate RF where claimed. Rollout remains owner-controlled.

## Minimum viable release boundary

**Drone Watch v1:** a truthful Remote ID message decoder, one **verified** supported receiver acquisition mode (onboard C6 if proven, or clearly identified compatible OrcNode), WATCH + AIRCRAFT + SOURCES pages, staleness, source health, and optionally MAP only after OrcMaps gate. No video, acoustic claims, proprietary-drone identification or arbitrary RF classification. A replay-only UI build is a **development demonstration**, never advertised as a functioning receiver.

## Evidence ledger (planning timestamp, not accomplishments)

| Phase | Status on 2026-10-26 | Acceptance file or missing gate |
| --- | --- | --- |
| P0 | RESEARCH DOCUMENTED; upstream/source audit incomplete | REA-Results draft PR #3; isolated 15/15 synthetic-model tests |
| P1 | NOT IMPLEMENTED | Host/parser vectors + malformed tests + pinned parser attribution |
| P2 | NOT RUN | C6 firmware/Hosted RX + Wi-Fi coexistence evidence |
| P3 | PARTIAL: W01 CODED / HOST-TESTED (1/1 CI suite); 24 screens NOT IMPLEMENTED | [W01 host CI](https://github.com/hardcoreerik/OrcSDR/actions/runs/40459435458); native build and physical display NOT RUN |
| P4 | NOT IMPLEMENTED | Actual RID capture + OrcMaps |
| P5 | NOT IMPLEMENTED | SD and OrcNode envelope |
| P6 | NOT RUN | SDR occupancy measurements (not drone ID) |
| P7 | NOT RUN | Analog receiver & digitizer real frames |
| P8 | NOT RUN | Mic recordings and classifier evaluation |
| P9 | NOT RUN | Version-pinned release acceptance |

**First implementation decision:** start P1 on a new implementation branch from current `main`; run P2 investigation in parallel on nonproduction hardware, but do not let either create a broken release dependency.
