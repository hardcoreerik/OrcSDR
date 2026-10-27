# Drone Watch — complete screen implementation reference

> **Version:** proposed UI/UX reference v1, 2026-10-26.  
> **One OrcSDR dashboard, five categories, 25 individually specified screens.** This is a design and engineering reference, **not 25 implemented screens**. Native build, ESP32-C6 receive, physical RF, video and acoustic gates remain open.
>
> **Read before working on ANY screen:** [OrcSDR UI rendering standard](../UI_RENDERING_STANDARD.md) → [our UI/UX contract](UI_UX_STANDARD.md) → [navigation](NAVIGATION.md) → [data/provenance](DATA_CONTRACTS.md) → [rendering tests](RENDERING_AND_ACCEPTANCE.md) → the exact screen file below.

## How Drone Watch is organized

- **Top-level:** one `DRONE WATCH` entry in OrcSDR Home / dashboard registry; **one ScreenController framebuffer owner** throughout all category changes.
- **Within it:** bottom five-category navigation `WATCH | AIRCRAFT | RF LAB | MEDIA | SYSTEM`; one simple secondary view selector for the category.
- **Entry point:** WATCH / LIVE WATCH. Open with zero reception sources and show truthful `SOURCE UNAVAILABLE` / `NO RID OBSERVED` states, not fake detections.
- **Expert tools:** advanced instrumentation is not required to understand the default view. Context taps route to Details, Inspector, Map, Sources and Timeline directly.
- **Release gating:** initial release can enable the proven pages and keep unimplemented experimental screens hidden or clearly explained; a screen spec is **not** a reason to enable a fake dead button.

## Screen-by-screen index

| Code | Menu label / screen document | What the user does | Earliest phase |
| --- | --- | --- | --- |
| **WATCH** ||||
| W01 | [LIVE WATCH](screens/watch/W01_LIVE_WATCH.md) | See current receiver health and most recent Remote ID broadcast | P3 mock, P4 live |
| W02 | [EVENT TIMELINE](screens/watch/W02_EVENT_TIMELINE.md) | Review actual decoded and source-state events in time order | P3 mock, P4 live |
| W03 | [ALERT RULES](screens/watch/W03_ALERT_RULES.md) | Set understandable local event/source alerts | P5 |
| W04 | [COVERAGE MONITOR](screens/watch/W04_COVERAGE_MONITOR.md) | Understand active listening intervals and interruptions | P4/P5 |
| W05 | [SESSION SUMMARY](screens/watch/W05_SESSION_SUMMARY.md) | Review observed message totals, source uptime and gaps | P4/P5 |
| **AIRCRAFT** ||||
| A01 | [AIRCRAFT LIST](screens/aircraft/A01_AIRCRAFT_LIST.md) | Browse received Remote ID records | P3 mock, P4 live |
| A02 | [AIRCRAFT DETAILS](screens/aircraft/A02_AIRCRAFT_DETAILS.md) | Read a broadcast's reported identity and fields | P3 mock, P4 live |
| A03 | [REMOTE ID INSPECTOR](screens/aircraft/A03_REMOTE_ID_INSPECTOR.md) | Inspect valid/invalid message types and parser provenance | P1 decoder, P3/P4 UI |
| A04 | [OBSERVED TRACK HISTORY](screens/aircraft/A04_OBSERVED_TRACK_HISTORY.md) | Follow genuine received location samples with visible gaps | P4/P5 |
| A05 | [ORCMAPS](screens/aircraft/A05_ORCMAPS.md) | Display valid *broadcast-reported* positions on existing map service | P4 |
| A06 | [ADS-B CONTEXT](screens/aircraft/A06_ADSB_CONTEXT.md) | Compare independent ADS-B and Remote ID context without identity fusion | P4 optional |
| **RF LAB** ||||
| R01 | [LIVE SPECTRUM](screens/rf_lab/R01_LIVE_SPECTRUM.md) | Inspect supported RTL-SDR spectrum and source validity | P6 |
| R02 | [WATERFALL](screens/rf_lab/R02_WATERFALL.md) | See measured spectral energy history with real gaps | P6 |
| R03 | [BAND OCCUPANCY](screens/rf_lab/R03_BAND_OCCUPANCY.md) | Measure occupancy during sampled scan dwell windows | P6 |
| R04 | [FREQUENCY PROFILES](screens/rf_lab/R04_FREQUENCY_PROFILES.md) | Apply legal/device-supported measurement/scan profiles | P6 |
| R05 | [SIGNAL COMPARISON](screens/rf_lab/R05_SIGNAL_COMPARISON.md) | Compare two measured IQ/spectrum snapshots with matched units | P6 |
| **MEDIA** ||||
| M01 | [VIDEO SOURCES](screens/media/M01_VIDEO_SOURCES.md) | Check physical analog receiver/digitizer signal chain | P7 |
| M02 | [LIVE ANALOG FPV](screens/media/M02_LIVE_ANALOG_FPV.md) | View real verified video frames from compatible external hardware | P7 |
| M03 | [MEDIA GALLERY](screens/media/M03_MEDIA_GALLERY.md) | Review optional actually saved media and verify integrity | P5/P7 |
| M04 | [ACOUSTIC MONITOR](screens/media/M04_ACOUSTIC_MONITOR.md) | Opt in to real microphone/audio measurements | P8 |
| **SYSTEM** ||||
| S01 | [SOURCES](screens/system/S01_SOURCES.md) | See supported, unknown, paused and absent receiving methods | P3/P4 |
| S02 | [ORCNODE NETWORK](screens/system/S02_ORCNODE_NETWORK.md) | Inspect independently validated paired remote collectors | P5 |
| S03 | [SESSION LOGBOOK](screens/system/S03_SESSION_LOGBOOK.md) | Start/verify/inspect optional saved session journals | P5 |
| S04 | [CAPTURE REPLAY](screens/system/S04_CAPTURE_REPLAY.md) | Feed deterministic test fixtures with persistent TEST DATA marker | P1/P3/P5 |
| S05 | [DIAGNOSTICS](screens/system/S05_DIAGNOSTICS.md) | Inspect firmware versions, errors and true receiver counters | P3/P9 |

## What each screen document guarantees as a spec

Every file contains:
1. Purpose and plain-English function, including the problem it solves.
2. **How to use:** the actual three-step operator flow.
3. **1280×720 layout:** content panel priorities with relative coordinates, safe shell ownership, fonts and 52px targets.
4. **Required data:** source and field provenance and what must be available before showing numbers.
5. **Control/hit behavior:** exact primary actions and destination routes.
6. **No data/error/paused/stale/replay:** truthful empty states and failure/recovery UI.
7. **Builder implementation recipe:** state owner, reuse points, modular files, nonblocking worker flow.
8. **720×720 adaptation:** reflow instead of clone.
9. **Screen-specific tests:** including host rendering and actual hardware gates.

**These are implementation contracts, not line-for-line CPP code.** Screen layout source should be refactored into reusable panels, status tiles, list rows, chart renderer and a single view route dispatcher, matching OrcSDR's existing module conventions.

## Reusable components to design once (proposed)

| Component | Reused by | Contract |
| --- | --- | --- |
| `DroneShell` | All 25 | Canonical header, one top-level owner, secondary selector, bottom five categories, modal/focus precedence |
| `SourceStatusCard` | W01, W04, A02, R01, M01, S01/S02/S05 | explicit source/mode/age/active/paused/error/unsupported |
| `ObservationRow` | W01/W02, A01/A02/A03 | stable event key, monotonic age, short ID, actual parser state |
| `FieldInspector` | A02/A03/A04, S05 | value/unit/validity/provenance, bounded ID/string layout |
| `PartialPlot` | W04, R01/R02/R03/R05, M04 | axes/grid static, bounded region update only, real measurements/gap markers |
| `MapPane` | A05 (and future contextual map) | call shared OrcMaps/OrcSDR map service; retain finished frame, labels preference and attribution |
| `AsyncActionButton` | W03/W05, R03/R04, M01/M02/M03, S02/S03/S04 | pending feedback first, verified success/error, no blocking storage/network work |
| `HelpOverlay` | All 25 | short plain-English meaning, overlay clips within safe region, blocks underlying hits |
| `TestDataBanner` | All replay-derived views | persistent explicit TEST DATA marking, including pins/media and export |
| `EvidenceBadge` | All status-oriented views | words and icons for measured vs claimed vs missing, never opaque 'threat %' |

## Suggested implementation packages and sequencing

### Slice A — Safe foundation, no radio changes (P1/P3)
Build `DroneShell`, all five category navigation, bounded mockable observation/source model, W01, A01, A02, A03 and S01 using fixtures. Add W02 and S05 for traceable history/health. Production builds show SOURCE UNAVAILABLE unless a verified acquisition adapter exists; don't ship seeded fake aircraft. Pass recording-display no-full-clear, touch and 720×720 geometry tests.

### Slice B — Actual RF and maps (P2/P4)
Qualify C6 on installed ESP-Hosted firmware with authorized Remote ID broadcaster; if not possible, use separately tested OrcNode source. Show received packet chain in W01/A01/A03/S01. Enable W04 coverage when real intervals exist; A05 maps only verified-valid reported coordinates through shared OrcMaps. A04 observed history only after real sequence tested. A06 ADS-B context remains optional read-only integration.

### Slice C — Useful persistence and networks (P5)
Add W03 alert rules, W05 session summary, S02 paired OrcNodes, S03 session journal and S04 replay. Avoid mandatory disk writes; test explicit privacy/retention and reconnect behavior. M03 gallery only for actual saved clips.

### Slice D — Optional experimental capabilities (P6–P8)
Reuse existing RF Lab for R01–R05, defer analog video M01–M02 until a supported receiver/digitizer displays true frames, defer M04 candidate interpretation until real microphone and negative/positive corpus prove classification. Disabled/unknown components explain requirements.

### Slice E — Release acceptance (P9)
Repeat existing OrcSDR regressions: FM/RDS, ADS-B, LoRa, Wi-Fi/ESP-Hosted/C6, SD, OrcMaps, Dead Air, USB host and IQ loss counters. Visual design accepted on **physical 5-inch Tab5** after host and native build gates; 720×720 tested independently. Preserve current map redraw limitations as an evidence-scoped exception, not a general excuse for new screen dropouts.

## Absolute build constraints

- Follow [`apps/orcsdr-tab5/ui/AGENTS.md`](../../apps/orcsdr-tab5/ui/AGENTS.md) and [`docs/UI_RENDERING_STANDARD.md`](../UI_RENDERING_STANDARD.md) whenever writing or changing UI.
- **No full-screen repaint on ordinary data/progress/time updates.** One UI owner. Workers send snapshots. Clip changed widgets and preserve overlays/map attribution.
- No per-page new full-screen sprite to solve flicker. Measure PSRAM/DMA memory, per-frame redraw, driver overruns and application IQ drop counters.
- Basic WATCH/AIRCRAFT experience approachable on a five-inch display; deeper technical pages available through one or two taps.
- Do not claim Remote ID RX based on presence of a C6, drone detection based on RF energy, general 5.8GHz capability from Blog V4, or viewable FPV from receiver RSSI.
- No background automatic logging of operator position, video or microphone. Explicit permission for capture and accepted privacy UI.
- Never turn **TEST DATA** into live state on return from replay.
- Separate `Implemented`, `Build-Verified`, `Hardware-Verified`, `RF-Verified` and `Experimental` in tests and user documentation.

## Related project documents

- [Drone Watch overview](README.md)
- [Architectural design](DESIGN.md)
- [Gated phases](PHASING.md)
- [Hardware/receiver validation](VALIDATION.md)
- [Local engineering handoff](HANDOFF.md)
- [REA-Results research PR #3](https://github.com/hardcoreerik/REA-Results/pull/3), private research archive (do not hand-edit generated REA indexes)
