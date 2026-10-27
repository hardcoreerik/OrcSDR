# W04 — COVERAGE MONITOR

> **Category:** WATCH · **Delivery gate:** P4 real source intervals; P5 full journaling · **Status:** proposed screen specification (no runtime implementation).  
> **Shared authority:** [UI contract](../../UI_UX_STANDARD.md) · [navigation](../../NAVIGATION.md) · [source model](../../DATA_CONTRACTS.md) · [rendering acceptance](../../RENDERING_AND_ACCEPTANCE.md).

## Purpose and default experience

Tell users precisely which radio methods were genuinely being monitored over time, and where gaps or unsupported methods prevent any conclusion about nearby aircraft.

## User how-to

1. Open COVERAGE from WATCH; read actual listening method, last confirmed active interval and paused time.
2. Select BLE4, BLE5, Wi-Fi Beacon, NAN or external source to inspect whether the firmware supports and actually uses that mode.
3. Tap a gap segment to see why listening stopped; return to WATCH with no scan mode changed.

## Exact Tab5 composition: 1280 × 720 reference

Shared OrcSDR header y0–113; secondary page selection y118–178; content safe area x20–1260/y188–634; bottom five-category bar y649–705. The following are **reference regions**, calculated through one common layout solver rather than hard-coded in each view:

- Wide main band x20–920: simple stacked horizontal colored time strips by source/method; blank/hatched segments are gaps, not 'quiet radio'.
- Right x940–1260: measured listening duration, monitoring gaps with reasons, last known active timestamp and method capability.
- Bottom note: COVERAGE IS NOT AIRSPACE CLEARANCE and clock quality/interval selected; bars have meaningful time ticks.

Use OrcSDR colors (black background, muted panel, cyan focus, green actual receiving, amber degraded); readable rows 20–26 px, headings 26–32 px, big status 36–56 px. Touch targets minimum 52px. Retain existing Home/volume/Visualizer/Settings/battery header controls.

### Actual displayed fields and dependencies

- Actual start/end monotonic intervals for active receive callbacks, paused/error/unsupported periods and source_id + transport.
- Observed frame count and missed-interval indicators where instrumented; ratio may be computed only from recorded active duration / selected observed window.
- Unknown time must remain unmeasured, not colored green. Do not infer 2.4/5.8 coverage from supported silicon.

## Controls, tap behavior and transitions

- Choose window (recent/this session) only if stored intervals permit; no historical time without journal.
- Tap time segment -> source event in W02 or source in S01; show why coverage paused during Wi-Fi/SDIO operations.
- Select method -> filter chart and right summary; never start a receiver automatically by chart tapping.

## Empty, disabled, stale and unreliable states

- No interval telemetry -> COVERAGE NOT INSTRUMENTED, not 0% or 100%.
- Confirmed active but zero frames -> MONITORING / NO RID OBSERVED; sensor offline -> NOT LISTENING.
- Session begins mid-window -> unknown portions visually distinct; replay mode includes TEST DATA.
- C6 BLE4 supported but Beacon/NAN not proved -> explicit UNKNOWN/UNSUPPORTED by method.

All source origin, aircraft identity, location, mode, timestamps and RSSI are evidence-bound. `TEST DATA` always visible during fixtures/replay. No `DRONE CONFIRMED`, `NO DRONES`, unverified pilot location, invented threat probability or continuous scanning claim. Source updates must not draw after losing screen ownership.

## Builder how-to and ownership

1. Have acquisition adapter emit explicit interval transitions from real start/stop/status callbacks; tolerate reset and monotonic wrap.
2. Compute bounded session coverage from intervals and excluded gaps; distinguish receiver enabled, active callback and packet arrival.
3. Draw time grid once, repaint only interval segments that changed or window scroll strip; do not animate a fake radar sweep.

Implement through `drone_watch_app` snapshot and WATCH view renderer under **one** `screens::Id::drone_watch`; route actions via [NAVIGATION.md](../../NAVIGATION.md). Never reimplement USB/RTL-SDR/C6/SD ownership in this page; UI cannot perform blocking network/storage tasks. Structural paint once on entry, value-only changes via dirty rectangles, no full-screen clear on timers. **Do not allocate a new 1280×720 framebuffer simply to draw this page.**

### Adaptive 720 × 720 rendering

One timeline method at a time with large previous/next source selectors; numeric coverage statement and gap list below. Preserve text meaning and state, not exact pixel coordinates; secondary page picker becomes Prev/Next or an accessible selector. No touch hit overlaps.

## Screen-specific acceptance tests

- Simulate 60 seconds with 10-second pause and check exact proportion only after intervals supplied.
- Unsupported method cannot produce a green lane; missing intervals produce unknown gray lane.
- Rapid transitions, clock reset, UI inactive and 100 idle ticks do not trigger full-screen clears.

Additionally pass the shared [renderer/reception regression matrix](../../RENDERING_AND_ACCEPTANCE.md): inactive updates emit no display calls, overlay blocking, no-signal state, geometry and physical flicker/readability on approved hardware. Record actual build/hardware outcomes separately—this document is a specification, not verification.
