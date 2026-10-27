# Drone Watch — OrcSDR dashboard planning

> **Status: PLANNED / DESIGN ONLY (2026-10-26).** No Drone Watch firmware dashboard, Remote ID receiver, live RF detector, video capture, or acoustic classifier is implemented by these documents. No hardware acceptance is claimed.

Drone Watch is a proposed **receive-only aircraft-awareness dashboard** for OrcSDR, with future OrcNode collection and OrcMaps display. Its product promise is: *show what the hardware actually received, explain what it means, and never confuse a signal with an identified aircraft.*

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
