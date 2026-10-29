# Drone Watch — OrcSDR dashboard planning

> **Status (2026-10-28 PT): W01 HOST-TESTED + P1 REMOTE ID CORE CODED/HOST-TESTED (3/3 suites).** The other 24 screens, C6 BLE/Wi-Fi Remote ID acquisition, analog video, and acoustics remain proposed. Native ESP-IDF build and physical hardware acceptance are NOT RUN.

Drone Watch is a proposed **receive-only aircraft-awareness dashboard** for OrcSDR, with future OrcNode collection and OrcMaps display. Its product promise is: *show what the hardware actually received, explain what it means, and never confuse a signal with an identified aircraft.*

## Screen-by-screen build specification (25 screens)

**[Open the complete Screen Reference Index → SCREENS.md](SCREENS.md)**

Drone Watch is one OrcSDR dashboard with five internal categories: **WATCH (5 screens), AIRCRAFT (6), RF LAB (5), MEDIA (4), SYSTEM (5)**. Each of the 25 screen documents under `screens/<category>/` is a specific implementation reference covering purpose, how-to, 1280×720 Tab5 regions, real data/provenance, touch actions, empty/error states, 720×720 adaptive behavior, and test gates.

Shared, **mandatory** contracts:

- [UI_UX_STANDARD.md](UI_UX_STANDARD.md): real OrcSDR shared header, touch/typography/color rules, partial-redraw/no-flicker rule and truth in measurement.
- [NAVIGATION.md](NAVIGATION.md): one ScreenController surface, five category routes, nested screens, back/selection state and modal focus.
- [DATA_CONTRACTS.md](DATA_CONTRACTS.md): typed source/observation validity, scan coverage, privacy, no fake drone identities, replay provenance.
- [RENDERING_AND_ACCEPTANCE.md](RENDERING_AND_ACCEPTANCE.md): M5GFX modular renderer, per-widget repaint and reproducible host/hardware acceptance.

**The screen reference is planning documentation; W01 alone has an initial native M5GFX implementation.** Host graphics compilation does not prove Bluetooth/Remote ID reception, onboard C6 capability, analog video or drone identification.

## First implemented screen — W01 Live Watch (host verified)

- `apps/orcsdr-tab5/ui/drone_watch_model.{hpp,cpp}` — UI-independent source truth, unavailable/paused/listening/replay classification.
- `apps/orcsdr-tab5/ui/drone_watch_dashboard.{hpp,cpp}` — M5GFX W01 landing page, source/help panels, disabled future-category tabs, compact square layout and partial updates.
- `apps/orcsdr-tab5/tests/drone_watch/` — host recording-display test of the **real renderer** and model against ScreenController. Its [README](../../apps/orcsdr-tab5/tests/drone_watch/README.md) provides reproducible commands and saves logs in the working folder.
- [CI workflow](../../.github/workflows/drone-watch-w01-host.yml) — compile+ctest host gate. **Passed:** [GitHub Actions W01 run](https://github.com/hardcoreerik/OrcSDR/actions/runs/40460255261), 1/1 test suite.
- The firmware consumer and Home registration are wired, but **native ESP-IDF / physical Tab5 are NOT RUN**. No receiver adapter is installed; opening W01 cannot detect Remote ID and does not retune RTL or flash C6.

**[Executed W01 checkpoint and exact verification limits](evidence/2026-10-27-w01-live-watch-host-checkpoint.md)**

## P1 Remote ID parser and bounded observation history

- `apps/orcsdr-tab5/ui/drone_watch_rid.{hpp,cpp}` — OrcSDR's bounded 25-byte/pack decoder wrapper, checked lengths and versions, safe optional claimed positions, and source/clock provenance.
- `apps/orcsdr-tab5/ui/drone_watch_rid_store.{hpp,cpp}` — 32-event bounded metadata history, per-source identity, monotonic order, replay marking and overwrite counters.
- `apps/orcsdr-tab5/ui/third_party/opendroneid/` — unmodified pinned Apache-2.0 OpenDroneID core + license/attribution; upstream blob SHAs matched exactly.
- `apps/orcsdr-tab5/tests/drone_watch/rid_test.cpp` — reference encoder and independently specified wire examples, malformed-frame boundaries, 5,000 deterministic fuzz inputs.
- `apps/orcsdr-tab5/tests/drone_watch/store_test.cpp` — replay, source separation, malformed rejection, 32-entry eviction and event accounting.
- [P1 host evidence checkpoint](evidence/2026-10-28-rid-core-host-checkpoint.md) and [3/3 passing host CI](https://github.com/hardcoreerik/OrcSDR/actions/runs/40826839560). No Bluetooth/Wi-Fi source or W01 live feed is connected; nothing here establishes real RF reception.

## Documents

- [DESIGN.md](DESIGN.md) — product scope, source and evidence contracts, proposed UI, module boundaries, concurrency, privacy.
- [PHASING.md](PHASING.md) — separately landable implementation phases, dependencies, exact exit gates and deferrals.
- [VALIDATION.md](VALIDATION.md) — repeatable host, RF, C6, map, UI and accessory test matrix, logging, rollback.
- [HANDOFF.md](HANDOFF.md) — local developer setup, first coding slice and how to verify without destabilizing the release.

## Research provenance

The separate **private** REA-Results draft PR [#3](https://github.com/hardcoreerik/REA-Results/pull/3) contains the dated, pinned source investigation:
- `inbox/2026-10-26_drone-watch-passive-reception-survey.md`;
- `inbox/2026-10-26_drone-watch-synthetic-evidence-test-record.md` (15/15 independent synthetic host tests; **not** protocol/RF evidence).

REA-Sandbox is the locally owned `F:\AI\RSA-Sandbox`; it was not run from the web-chat environment, and its generated `targets/`, `comparisons/` and indexes were not edited.

## Related existing OrcSDR subsystems

`apps/orcsdr-tab5/ui/dashboard_registry.{hpp,cpp}`, `screen_controller.{hpp,cpp}`, `dead_air_app.{hpp,cpp}`, `adsb_dashboard.{hpp,cpp}`, `wifi_service.{hpp,cpp}`, and `offline_map.{hpp,cpp}`. Read `Roadmap.md`, `PROJECT_STATUS.md` and `docs/user-guide/wifi-tab5.md` before implementation. Existing root `phasing.md` is an historical ledger; this is **the new feature plan**, not an edit to that ledger.

No third-party source has been committed here. License compatibility and attribution are gates, not assumptions.
