# W05 — SESSION SUMMARY

> **Category:** WATCH · **Delivery gate:** P4 bounded current-session counters; P5 saved export · **Status:** proposed screen specification (no runtime implementation).  
> **Shared authority:** [UI contract](../../UI_UX_STANDARD.md) · [navigation](../../NAVIGATION.md) · [source model](../../DATA_CONTRACTS.md) · [rendering acceptance](../../RENDERING_AND_ACCEPTANCE.md).

## Purpose and default experience

Give a simple factual recap of the current listening session: methods actually used, messages seen, source downtime and data completeness, without constructing a fictional flight log.

## User how-to

1. Tap SESSION SUMMARY; review session start (relative if clock unavailable), elapsed time and observed source coverage.
2. Inspect message-kind counts and system issues; tap a count to open Timeline filtered to those records.
3. When opt-in journaling is supported, save an explicitly named session summary and verify success before leaving.

## Exact Tab5 composition: 1280 × 720 reference

Shared OrcSDR header y0–113; secondary page selection y118–178; content safe area x20–1260/y188–634; bottom five-category bar y649–705. The following are **reference regions**, calculated through one common layout solver rather than hard-coded in each view:

- Large summary tile x20–620: session duration and REMOTE ID MESSAGES received, highlighted as counts of messages not aircraft.
- Right x640–1260: source-by-source active time, pause/gap count, last observation and loss/drop totals.
- Bottom content: DETAILS / VIEW EVENTS / SAVE SESSION (only after P5 opt-in storage); no speculative distance or flights chart.

Use OrcSDR colors (black background, muted panel, cyan focus, green actual receiving, amber degraded); readable rows 20–26 px, headings 26–32 px, big status 36–56 px. Touch targets minimum 52px. Retain existing Home/volume/Visualizer/Settings/battery header controls.

### Actual displayed fields and dependencies

- Session start monotonic, optional UTC and quality, observation count, valid RID per message kind and parser error counts.
- Unique broadcaster grouping count only if validated policy exists, otherwise omit; separate RF-only, video and acoustic event counts.
- Actual source active/gap durations, drop counters, storage state, whether exported and checksum if successful.

## Controls, tap behavior and transitions

- VIEW EVENTS -> W02 prefiltered to current session; SOURCE -> S01; SAVE -> S03 session logbook worker if enabled.
- RESET SESSION is a separate confirm action and must not erase SD silently.
- Only show export/CSV format if an implemented and verified exporter exists; never promise a file on unsupported storage.

## Empty, disabled, stale and unreliable states

- No listener: START MONITORING A SOURCE, with session duration only if session exists.
- Active but no packets: zero observed messages, not zero drones; source pause highlighted.
- Clock invalid -> SESSION DURATION valid / WALL TIME UNAVAILABLE.
- Failed save -> NOT SAVED with retry and no false success, preserving RAM summary.

All source origin, aircraft identity, location, mode, timestamps and RSSI are evidence-bound. `TEST DATA` always visible during fixtures/replay. No `DRONE CONFIRMED`, `NO DRONES`, unverified pilot location, invented threat probability or continuous scanning claim. Source updates must not draw after losing screen ownership.

## Builder how-to and ownership

1. Use one session accounting owner from Data Contracts; snapshot counters from event/source model, not view-local tallies.
2. Compute visual totals on bounded ticks and repaint only changed card values; async save/log worker supplies status.
3. Treat persisted sessions as separate IDs with immutable provenance, not accumulated current data after reset.

Implement through `drone_watch_app` snapshot and WATCH view renderer under **one** `screens::Id::drone_watch`; route actions via [NAVIGATION.md](../../NAVIGATION.md). Never reimplement USB/RTL-SDR/C6/SD ownership in this page; UI cannot perform blocking network/storage tasks. Structural paint once on entry, value-only changes via dirty rectangles, no full-screen clear on timers. **Do not allocate a new 1280×720 framebuffer simply to draw this page.**

### Adaptive 720 × 720 rendering

Three full-width stacked summary cards with DETAILS action; don't squeeze a two-column analytics dashboard into a 720-square window. Preserve text meaning and state, not exact pixel coordinates; secondary page picker becomes Prev/Next or an accessible selector. No touch hit overlaps.

## Screen-specific acceptance tests

- Ten packets from one claim show 10 messages, not 10 drones.
- Known paused interval appears in downtime and does not count as active coverage.
- SD missing/save failure emits distinct error; source switch and 100 clock ticks preserve layout and full-screen-clear count.

Additionally pass the shared [renderer/reception regression matrix](../../RENDERING_AND_ACCEPTANCE.md): inactive updates emit no display calls, overlay blocking, no-signal state, geometry and physical flicker/readability on approved hardware. Record actual build/hardware outcomes separately—this document is a specification, not verification.
