# AIS Dashboards — Native Tab5 / M5GFX implementation tracker

> **Branch:** [`chatgpt/ais-dashboard-20261106`](https://github.com/hardcoreerik/OrcSDR/tree/chatgpt/ais-dashboard-20261106)  
> **Community request:** [OrcSDR Issue #234 — AIS vessel decoding](https://github.com/hardcoreerik/OrcSDR/issues/234)  
> **Scope:** original receive-only C/C++ AIS decoder and seven-panel native M5GFX interface  
> **Updated:** 2026-11-07  
> **Status (2026-11-10):** **Wired into the Tab5 application (Home > All Dashboards > AIS) and decoding synthetic AIS IQ replayed over rtl_tcp on a physical Tab5. Not yet tested with real ships, in a sustained USB soak, or against another decoder on identical real IQ.** See [docs/AIS_INTEGRATION_CHECKPOINT.md](docs/AIS_INTEGRATION_CHECKPOINT.md) and [docs/evidence/ais/2026-11-10-tab5-replay.md](docs/evidence/ais/2026-11-10-tab5-replay.md). The earlier note below, written before integration, is kept as history.  
> **Research separation:** RSA/REA standards/competitor findings stay exclusively in the separate private `REA-Results` workflow, **not in this development branch**.

## Design commitment

OrcSDR AIS is not just a ship radar. The product must ultimately decode all **28 defined AIS message types** of ITU-R M.1371-6 (2026), including Aids-to-Navigation 21/28, SAR aircraft, base stations, control/safety messages, long-range report 27, and application-specific binary data through a version-aware DAC/FI registry. Unknown application payloads retain their raw bytes and are described as **uninterpreted**, never made-up facts.

The native decoder, message interpretation, receiver transport, and M5GFX interface must remain independent modules. Reference software is an **independent benchmark**, not a source of code to incorporate.

The UI must accurately represent observed facts. A missing antenna, incomplete parser, disconnected RTL-SDR, stale vessel coordinates, unknown callsign, unverified location, unsupported DAC/FI, or absent map tile **must never be replaced by synthetic radio data**. Demo illustrations are only generated outside the production renderer for design review.

## Existing code — implemented on this branch

| Item | Path | Status |
|---|---|---|
| Seven-page frame geometry / constexpr tests | `apps/orcsdr-tab5/ui/ais_dashboard_layout.hpp` | **Implemented** |
| Bounded snapshot/data/action contract | `apps/orcsdr-tab5/ui/ais_dashboard.hpp` | **Implemented** |
| Native M5GFX renderer / panel and touch logic | `apps/orcsdr-tab5/ui/ais_dashboard.cpp` | **Implemented; host render test passing** |
| Screen ownership identifier and name | `apps/orcsdr-tab5/ui/screen_controller.{hpp,cpp}` | **Implemented** |
| Component CMake registration | `apps/orcsdr-tab5/main/CMakeLists.txt` | **Implemented** |
| Host M5GFX render/bounds/selection/empty-state tests | `tests/ais_dashboard_render_tests.cpp` | **Implemented; GitHub Actions host test passing** |
| Dedicated host CI workflow | `.github/workflows/ais-dashboard-host-tests.yml` | **Implemented** |
| Portable original AIS protocol parser (header coverage 1–28, mapped position/AtoN fields) | `apps/orcsdr-tab5/ui/ais_protocol.{hpp,cpp}` | **Host-tested synthetic fixtures; not full 28-type field decoding** |
| Bounded identity-safe AIS station store | `apps/orcsdr-tab5/ui/ais_station_store.{hpp,cpp}` | **Host-tested; not connected to receiver or UI** |
| Portable AIS parser/store sanitizer test + CI | `tests/ais_protocol_tests.cpp`, `.github/workflows/ais-protocol-host-tests.yml` | **Sandbox ASan/UBSan passed; CI pending** |
| Original dual-channel AIS 240 kS/s CU8 GMSK/HDLC/FCS receiver | `apps/orcsdr-tab5/ui/ais_receiver.{hpp,cpp}` | **Synthetic IQ-to-CRC packet tests passed; no live IQ** |
| Radio-session-generation-checked packet/parser/station pipeline | `apps/orcsdr-tab5/ui/ais_receive_pipeline.{hpp,cpp}` | **Host integrated synthetic-IQ test passed; not wired to DSP task** |
| IQ receiver sanitizer tests, host benchmark and CI | `tests/ais_receiver_tests.cpp`, `tests/ais_receiver_bench.cpp` | **Host tests passed; physical P4 performance unknown** |
| Actual home/menu/runtime navigation into AIS | `ais_app` / top-level UI dispatcher | **Not implemented** |
| Actual RF IQ-to-AIS decoder and receiver callbacks | Native DSP and parser modules | **Not implemented** |
| Real Tab5 flash and touch verification | Hardware test | **Not performed** |
| Genuine AIS vessel rendering on physical Tab5 | Real RF validation | **Not performed** |

**Do not confuse a successful host compilation of the seven panels with a completed on-device radio feature.** The dashboard is presently compiled into the firmware component, but not yet reachable through the live home/menu dispatcher. Its `enter()` requires `screens::Id::ais` ownership to have been granted.

## OrcSDR visual and ownership rules

- **Physical panel:** M5Stack Tab5 in **1280 × 720 landscape**; all rendering by M5GFX / M5Unified, no browser or web stack.
- **Native chrome:** `audio_header::draw_brand("AIS")` and native OrcSDR badge, battery, Home, Audio/VIS and Settings buttons. Never draw in the reserved status area (`x420..866, y12..66`).
- **Layout:** header through y104; context `x20..1260, y114..148`; content y158..636; seven tactile tab buttons at **y650..702**, x20 + 178 × index, 168 px wide.
- **Color/palette:** black background; OrcSDR panel RGB565 `0x0841`; edge `0x2945`; cyan `0x2e7f`; green `0x6fe8`; amber `0xfd20`; red `0xf800`; muted text `0x8c71`. No unrelated theme.
- **Screen ownership:** `screen_controller` remains the single framebuffer owner. `enter` requires active AIS screen; `update` / touches require ownership; never write framebuffer from radio/ISR callback.
- **PSRAM:** snapshot allocated via ESP-IDF SPIRAM allocation; body rendering uses up to two 1280×522 RGB565 PSRAM sprites; if allocation fails, fall back to native direct painting rather than failing or using internal DMA-reserved RAM.
- **Flicker:** compose whole body offscreen, compare RGB565 tiles with existing `acars_ui::diff_rects`, push only changed regions when possible. After page changes or significant diffs use native direct primitives; no indiscriminate whole-screen clear in routine updates.
- **No fabricated observation:** the production `Snapshot{}` state shows empty lists, placeholder fields and warnings. Simulated map points, packets and spectra exist **only in external preview PNGs** and must always be labeled as illustrative.

## Seven panels and precise scope

| Page | Core UI delivered | Runtime/data still required |
|---|---|---|
| **1. Live Map** | Large local OrcMaps-backed projection (when receiver coordinates valid), type-distinct markers, tap-to-select target, zoom 10/25/50/100 NM, activity side panel, selected MMSI summary | Feed vessel/AtoN positions, sector/track history, own receiver location, basemap packs, dynamic freshness; no nautical-chart claims |
| **2. Targets** | Scrollable station list, MMSI, class, age; selected identity/position/course/destination, stale flag | Merge Class A/B static & movement fields, AtoN, SAR and shore stations, stable freshness model, tracks/filters |
| **3. Messages** | Bounded live journal by channel/type/MMSI/age; CRC status; raw hex display with truncation flag; selectable message | Actual parser snapshots, more granular field inspector, RF CRC and NMEA checksum separation, replay/archive and filters |
| **4. Protocol** | Indexed browser of **all 28 message type names**, actual journal coverage count, special handling labels for AtoN 21/28 and long-range type 27 | Full per-field decoded schema and bit ranges for **every** type, DAC/FI registry, raw bit diagram, unknown handling, provenance |
| **5. Signal** | Separate AIS 1/AIS 2 measured FFT inputs, sample rate, CRC pass/fail, IQ drop counts, input transport | DSP health metrics, measured PPM/clock drift, signal plots, runtime CPU/PSRAM costs, benchmarked dense-channel behavior |
| **6. Events** | Timestamped AIS event list, visually distinct safety vs normal events, station/shore classifications | Real event derivation; rule tuning, safety text handling, optional logs; no unsupported collision/CPA alarms |
| **7. Setup** | Current receiver mode, both RF channels, long-range mode, NMEA and logging status, action-returning touch surfaces | App-side action acknowledgement/persistence, receiver switching, authenticated NMEA output, SD policy, channel/tuning management |

### Interactions implemented in the UI renderer

- Seven touch tabs; default entry to Live Map.
- Select target from Targets list or valid positioned map marker.
- Up/down paging for targets, messages, protocol and events.
- Select message by row; show its actual bounded raw packet hex and reception metadata.
- Select any of the 28 protocol types in a scrolling index.
- Map zoom action (10 / 25 / 50 / 100 NM), no implicit retune.
- Setup toggles **return actions** to an application adapter. They do *not* silently modify device hardware, network, or recording state before acknowledgement.
- Header Home and Settings return requests to the owner rather than modifying the global navigation state inside the dashboard.

### Important current limitations

1. No actual AIS `ais_app` session/decoder/receiver adapter, no feature menu entry, and no ship traffic on physical hardware yet.
2. On the Type 1–28 Protocol view, **message names, categories and journal counts are real UI code**, but the complete field-by-field inspector is a subsequent milestone. Do not claim 28 decoders exist yet.
3. Stale-position policy and raw message storage need validation under burst traffic. A selected MMSI never silently changes to an unrelated target.
4. The map uses existing OrcMaps on-device; it is **not a certified nautical chart**. Basemap failure must not erase decoded coordinates or claim coverage.
5. The seven external PNG previews are **illustrations**, not screenshots from Tab5 hardware or received AIS samples.
6. Some advanced user features (target filtering/search, track timeline, NMEA SD archive, TDMA slot view, AIS decoder performance graph) are planned, not implemented.

## Canonical data and module boundary

Proposed pipeline (each lane independently testable):

```text
USB RTL-SDR / future rtl_tcp IQ source
  -> radio_session / source lifetime and generation
  -> AIS dual-channel frontend + GMSK/NRZI/HDLC/FCS (original C/C++)
  -> message parser: types 1..28 + DAC/FI extension registry
  -> bounded station store + recent-message journal + event queue + RF stats
  -> UI-task-owned, immutable ais_ui::Snapshot
  -> ais_ui::enter / update / handle_touch (7 M5GFX pages)
  -> existing OrcMaps / navigation / SD and opt-in NMEA adapters
```

The tab UI does not own USB receiver state. Do not introduce a second RTL-SDR pipeline that competes with `radio_session`. Long-range Type 27 uses 156.775/156.825 MHz and needs an alternate tuning mode or a second radio; it cannot be captured simultaneously in the 162.000 MHz narrowband AIS1/AIS2 passband.

**Bounded preview snapshot:** up to 32 station entries, 16 recent messages, 14 events and two × 64 true-spectrum bins. These are current UI capacities, **not** total decoder tracking limits. Overflow/virtualization and memory tests should establish the final on-device capacities.

## Validation / acceptance checklist

- [x] Seven native M5GFX page render functions and layouts committed on AIS-only branch
- [x] Existing shared header and status reserve honored by geometry
- [x] Native OrcSDR colors, PSRAM sprites and ACARS dirty-region technique reused
- [x] Host tests exercise all seven, empty state, selection, button actions, and bounds
- [x] Host CI workflow created and first run passed
- [ ] Entire on-device ESP-IDF build and Tab5 / XTR V5 flash performed
- [ ] Actual AIS navigation/home menu route implemented; screen ownership handoff and return path tested
- [ ] Native original AIS decoder/parser for 1–28 and extensible DAC/FI implemented (partial field core implemented; full field coverage pending)
- [ ] Live receiver source and dual-channel IQ snapshots connected to UI (standalone dual-channel 240k IQ receiver host-tested, not yet wired to device/runtime)
- [ ] Event/AtoN/position/track semantics validated with known-good message fixtures
- [ ] Real IQ packet parity, per-channel rate, CRC and memory/CPU tests completed
- [ ] Map UX: marker collision, pan/zoom, tile attribution, coast/no-tile fallback, target sorting/filters verified
- [ ] UI refresh measured: frame time, partial-push area, touch responsiveness, internal/PSRAM allocations
- [ ] Running device storage logs saved in same working folder as test runner where appropriate, without changing RSA/REA publish rules
- [ ] CodeRabbit findings and on-device acceptance addressed before requesting merge
- [ ] Explicit maintainer permission obtained **before any merge**

## Visual review record

Seven **1280×720 illustrative design preview PNGs** were created in the ChatGPT working container for review: Live Map, Targets, Messages, Protocol, Signal, Events, Setup. They preserve the firmware geometry and OrcSDR dark palette, but the Python/Pillow lettering is only an approximation of the M5GFX font and the displayed AIS messages/ship locations are **fictional design values**. They must not be checked in as RF test evidence.

This document is the state tracker for implementation, panel review feedback, follow-up work and on-device acceptance. Update the checklist and actual evidence paths after each development milestone.

## Implementation order after the first UI pass

1. Confirm field names and full Type 1–28 display requirements; add per-field/raw-bit inspector and DAC/FI subview.
2. Add `ais_app` that owns bounded message/target/event snapshots and action dispatch with controlled receiver life-cycle.
3. Wire AIS into Marine and normal home/dashboard routing, then verify returns, status header and keyboard focus navigation.
4. Implement/test original AIS RF and message decoders and connect their validated events to the view.
5. Benchmark in the real Tab5 display environment (UI update cost, touch latency, RF overrun, memory).
6. Use the independent RSA/REA results (kept in REA-Results) as reference test evidence, never as research text committed into this branch.

## 2026-11-07 — native protocol/state foundation (host-only)

- Added a portable, original C++17 bitfield parser with preliminary message-length guards for IDs 1–28; the general case is deliberately reported as `Coverage::header_only`. **Header coverage is not full 28-type decode coverage**.
- Field extraction implemented for Class A position reports (1/2/3), Class B positions (18/19), Type 21 AtoN name/extension and virtual flag, Type 27 long-range location/motion, and **Type 28's 168-bit field map**. Type 28 Source ID and geometry-dependent dimension fields are preserved, not reinterpreted as Type 21 or as invented polygons.
- Added a fixed-capacity (64 entries) station store with separate MMSI vs Type 28 Source-ID namespaces, distinct last-heard / last-valid-position / last-static timestamps, eight actual fixes per station, bounded overflow rejection and deterministic pruning.
- Added specification-derived **synthetic** fixture tests for every type's header/length, selected full field maps, unavailable/sentinel positions, malformed input, out-of-order timestamps, history bounds, station overflow, and source-ID collision. Local host build with `-Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined` **passed**. This tests our implementation against its fixtures, not independent field-level or real-RF parity.
- Sources are registered in ESP-IDF CMake, but **no ESP32-P4 full firmware build, flash or timing measurements were performed**. The existing seven M5GFX panels and OrcMaps library were not modified by this milestone.
- **Critical integration work still pending:** AIS GMSK/IQ/HDLC/CRC receiver, full per-field parsing for the remaining message types, binary DAC/FI registry, static message 5/24 merging, UI `Snapshot` adapter and identity-kind-aware selection (Type 28 Source IDs must **never** be labeled MMSIs), OrcMaps consumer overlay adapter after the independent OrcMaps feature API is ready, and live Tab5 acceptance.
- **Separate radio modes:** Type 27 long-range reports need 156.775 / 156.825 MHz tuning, not reception from a 162 MHz dual-channel IQ window.

Host reproduction from the OrcSDR repository root:

```bash
g++ -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Iapps/orcsdr-tab5/ui \
  tests/ais_protocol_tests.cpp \
  apps/orcsdr-tab5/ui/ais_protocol.cpp \
  apps/orcsdr-tab5/ui/ais_station_store.cpp \
  -o /tmp/ais-protocol-tests
/tmp/ais-protocol-tests
```

Expected: `AIS portable protocol and bounded station tests passed (synthetic fixtures only)`.

## 2026-11-07 — dual-channel synthetic-IQ AIS receiver foundation

OrcSDR now contains an **original** portable `ais_rx::Receiver` processing CU8 IQ centered at **162.000 MHz / 240,000 complex samples per second**. It splits AIS1 (161.975 MHz) and AIS2 (162.025 MHz) into two independently mixed, 63-tap FIR-filtered, decimated (48 kS/s) receive paths. Each channel uses a complex phase discriminator, slow DC/CFO rejection, 5 fixed 9600-symbol/s timing hypotheses, NRZI bit reconstruction (hold=1), HDLC flag detection, bit destuffing, and reflected X.25/HDLC CRC-16 residue validation (0xF0B8). The first CRC-valid packet from concurrent hypotheses wins; time-local identical hypothesis duplicates are suppressed.

`ais_rx::Pipeline` joins validated RF packets to the existing portable AIS parser and bounded station store. Its input contract requires center frequency, rate, nonzero radio-session generation and monotonic time. It rejects mismatched source configurations, resets the DSP after generation changes or input gaps, and never takes ownership of USB hardware. A single owning DSP task must call it; the UI must read a separately published immutable snapshot, **not** a mutable station store. See `docs/AIS_RECEIVER_ACCEPTANCE.md`.

**Actual tested:** Native C++17 ASan/UBSan host build, specification-derived reflected CRC check (123456789 → 0x906E), 168-bit Type 1 synthetic GMSK payloads recovered on each channel, simultaneous two-channel signals, source chunk boundaries, polarity inversions, +/-800 Hz carrier offset in synthetic data, corrupted FCS rejection, maximum 1008-bit HDLC payload, random IQ noise rejection, discontinuity and wrong-rate handling, and an IQ→packet→parser→station end-to-end fixture. An independent no-signal host optimization benchmark is provided; **host processing speed is not Tab5 performance evidence**.

**Not done/blocked:** Device-radio owner selection and IQ callback wiring; full M5GFX snapshot bridge; actual marine traffic, real captured IQ or external AIS-catcher parity; ESP32-P4 firmware compile/flash; PLL/adaptive timing and interference/weak-signal sensitivity evaluation; full protocol field coverage (only selected message types are parsed beyond their header). Type 27 requires separate 156.775/156.825 MHz tuning. AIS RF FCS failures must not be conflated with NMEA transport checksums.

No OrcMaps, RSA, REA or unrelated SDR features were modified by this milestone. No claims about being the fastest, most sensitive or best receiver are justified before controlled A/B measurements.

## 2026-11-07 — AIS dashboard identity, map fallback and station snapshot seam

**Scope:** OrcSDR dashboard/model improvement only, still before device deployment. No radio ownership changes, OrcMaps source changes, or full Tab5 firmware acceptance.

- **Correct identity:** `ais_ui::Target`, `Message` and `Event` now carry an explicit `IdentityKind`. Type 28's 30-bit Source ID is stored separately from a conventional MMSI; list and inspector labels say **SOURCE ID**, not MMSI. Tap selection and `ActionKind::target_selected` return both identifier number and kind so numerical collisions cannot silently select a different maritime entity. Older MMSI-only snapshot callers remain compatible through default fields.
- **AtoN classification:** The 2026 AtoN report has a separate presentation class; no virtual/physical claim is inferred from raw Type 28 dimensional fields.
- **Location-independent map:** The dashboard uses a valid configured receiver fix when available, otherwise an explicitly selected valid AIS target, otherwise its previous stable target anchor, otherwise the first non-stale valid AIS position. Unknown or invalid receiver coordinates no longer suppress geographic display. The policy lives in portable `ais_map_policy.hpp`, independently tested without hardware. Selection and map touch now work even if the receiver location is unknown.
- **Bounded station-to-UI adapter:** `ais_snapshot_adapter` translates actually observed `ais::StationStore` entries into the existing bounded `ais_ui::Snapshot`. It prioritizes the newest 32 of up to 64 stations, preserves source identity, station class, fix and update ages, unavailable fields, and up to eight *recorded* positions per station. It does not invent course, heading, names or movement. No dynamic allocation is performed by this adapter.
- **Clear missing/stale data:** The Targets details show missing positions as missing, and explicitly mark stale position ages.
- **Verification:** The updated production M5GFX seven-panel rendering fixture tests Type 28/MMSI numeric collisions and selection. Dedicated pure C++ tests cover stable anchor preference, absent receiver coordinates, stale/invalid positions, independent MMSI/Source-ID selection, station age, chronological history, missing fixes, and dense-store snapshot prioritization. See GitHub Actions **AIS dashboard host rendering** for the actual executed results.

**Not done:** The independent OrcMaps generic overlay API is not yet consumed or pinned in OrcSDR. The compact map continues using its existing map projection plus simple circles, and an offscreen body canvas may still require expensive basemap recomposition on an ordinary snapshot revision. No physical device draw, touch, memory, CPU or IQ-continuity acceptance was performed; this change does not prove real RF reception. Do not treat Type 28 raw dimension fields as geometry without interpretation verified against the 2026 specification.

**Host reproduction** (from OrcSDR repo root; no device required):

```bash
mkdir -p ais-test-runs
g++ -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Iapps/orcsdr-tab5/ui \
  tests/ais_map_policy_tests.cpp -o ais-test-runs/ais-map-policy-tests
./ais-test-runs/ais-map-policy-tests 2>&1 | tee ais-test-runs/ais-map-policy-tests.log

g++ -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Iapps/orcsdr-tab5/ui \
  tests/ais_snapshot_adapter_tests.cpp \
  apps/orcsdr-tab5/ui/ais_snapshot_adapter.cpp \
  apps/orcsdr-tab5/ui/ais_station_store.cpp \
  -o ais-test-runs/ais-snapshot-adapter-tests
./ais-test-runs/ais-snapshot-adapter-tests 2>&1 | tee ais-test-runs/ais-snapshot-adapter-tests.log
```

M5GFX recording-host render tests use `.github/workflows/ais-dashboard-host-tests.yml`. **These tests validate synthetic presentation fixtures, not live maritime observations.**

## 2026-11-07 — international NMEA + DAC/FI validation milestone

- Added original portable bounded binary application envelope parser for AIS Type 6, 8, 25 and 26, including structured/unstructured flags, 10-bit DAC and 6-bit FI, bit-precise opaque payload retention and distinct Type 26 radio communication-state handling. No new IQ receiver or SDR ownership was introduced.
- Field decoding is implemented **only** for three official IALA registered variants: IMO 6/1/16 persons aboard, EU inland 6/200/55 persons, and EU inland 8/200/10 static/voyage data. All other application bodies are **preserved unknown** until field-level independent reference validation exists.
- Added optional nonblocking binary-application callback and counters at the existing CRC-valid packet seam; unchanged RF demodulation, receiver tuning and UI rendering.
- Added checksum-checked Japanese NMEA excerpt fixtures drawn from 2013 maritime research. Text-extraction examples with invalid checksums are explicitly rejected; neither source is IQ.
- Added a version-pinned independent worldwide 2025 AIS Hub NMEA corpus manifest and an on-demand checksum/armoring/multipart/identity/DAC/FI validator. Tests download to a local temporary cache; original third-party data is **not vendored** without verified redistribution rights.
- Recorded Global Fishing Watch hourly global AIS vessel presence as a potential **map-density research source only**, subject to noncommercial license and API constraints. It cannot validate raw RF or individual AIS messages.
- Added CI for synthetic field/sentinel tests plus independent real NMEA host regression. Exact results must be taken from GitHub Actions after this change, not inferred from implementation.
- Detailed provenance, source links, security/accuracy boundaries and host reproduction commands: `docs/AIS_INTERNATIONAL_VALIDATION.md`.

**Unverified:** physical Tab5 / ESP32-P4 compilation and reception, Japanese/Russian IQ recordings, full 28-type semantic coverage, complete IALA DAC/FI implementations, unknown binary data presentation in M5GFX, and full field-by-field independent oracles.
