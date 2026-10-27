# W02 — EVENT TIMELINE

> **Category:** WATCH · **Delivery gate:** P3 replay history; P4 real events; P5 persisted sessions · **Status:** proposed screen specification (no runtime implementation).  
> **Shared authority:** [UI contract](../../UI_UX_STANDARD.md) · [navigation](../../NAVIGATION.md) · [source model](../../DATA_CONTRACTS.md) · [rendering acceptance](../../RENDERING_AND_ACCEPTANCE.md).

## Purpose and default experience

Present a chronological account of what Drone Watch actually observed—decoded broadcasts, source pauses/reconnections and optional RF/video/acoustic events—with an auditable filter and source origin.

## User how-to

1. From WATCH tap VIEW TIMELINE; the newest event appears at the top, with time relative to actual reception.
2. Use EVENT TYPE or SOURCE chips to narrow the view without changing the receiver's capture mode.
3. Tap a row for the decoded observation, source change or clip reference; return with filters and list position preserved.

## Exact Tab5 composition: 1280 × 720 reference

Shared OrcSDR header y0–113; secondary page selection y118–178; content safe area x20–1260/y188–634; bottom five-category bar y649–705. The following are **reference regions**, calculated through one common layout solver rather than hard-coded in each view:

- Left list x20–870: 4–6 large timestamped rows, with event icon, plain label, acquisition method and age; source-specific marker is always text too.
- Right x890–1260: selected event summary, raw/decoded status, linked SCREEN target and source ID; use DETAIL instead of speculative map or risk score.
- Filter bar in upper content with two 56px controls; pagination/scroll control bottom of list rather than small scrollbar-only target.

Use OrcSDR colors (black background, muted panel, cyan focus, green actual receiving, amber degraded); readable rows 20–26 px, headings 26–32 px, big status 36–56 px. Touch targets minimum 52px. Retain existing Home/volume/Visualizer/Settings/battery header controls.

### Actual displayed fields and dependencies

- Bounded event ring with stable event_key, event_kind, rx_monotonic_ms, optional UTC/clock quality, source_id, transport, parser status and value validity.
- Explicit coverage and source_state-change events; LOST/DROPPED counts must have separate system events rather than silently missing rows.
- Optional persisted session reads only after P5 and on storage worker, not from UI draw.

## Controls, tap behavior and transitions

- Change type filter: RID, SOURCE, RF, VIDEO, AUDIO, ALL; unavailable families remain disabled/explained.
- Select event: RID -> A02/A03; source change -> S01; RF -> R01; video clip -> M03 only if capture exists.
- Tap CLEAR VIEW resets RAM filter/selection, not persisted session or source; separate deletion confirmation later.

## Empty, disabled, stale and unreliable states

- No events while active -> NO EVENTS OBSERVED with live listener state; source off -> NOT MONITORING.
- Log buffer overflow -> EVENT HISTORY INCOMPLETE with dropped count; never infer unseen events.
- No valid UTC -> elapsed/relative ordering; wall time shown only when synchronized.
- Replay mode -> TEST DATA marker repeated near timeline header and detail.

All source origin, aircraft identity, location, mode, timestamps and RSSI are evidence-bound. `TEST DATA` always visible during fixtures/replay. No `DRONE CONFIRMED`, `NO DRONES`, unverified pilot location, invented threat probability or continuous scanning claim. Source updates must not draw after losing screen ownership.

## Builder how-to and ownership

1. Reuse same central EventModel and source-state stream as WATCH; no duplicate event parser.
2. Create bounded paged ring adaptor, stable selection and row dirty updating; append only draws new/shifted rows, not full screen.
3. Store filters in view route; page swap may invalidate list, timer age update repaints text cells only.

Implement through `drone_watch_app` snapshot and WATCH view renderer under **one** `screens::Id::drone_watch`; route actions via [NAVIGATION.md](../../NAVIGATION.md). Never reimplement USB/RTL-SDR/C6/SD ownership in this page; UI cannot perform blocking network/storage tasks. Structural paint once on entry, value-only changes via dirty rectangles, no full-screen clear on timers. **Do not allocate a new 1280×720 framebuffer simply to draw this page.**

### Adaptive 720 × 720 rendering

One vertical list with 3 large rows; tapping opens full-page event details and visible BACK; no persistent side inspector. Preserve text meaning and state, not exact pixel coordinates; secondary page picker becomes Prev/Next or an accessible selector. No touch hit overlaps.

## Screen-specific acceptance tests

- Mix valid RID, malformed, pause, SD failure and RF-only samples; verify each row icon/label retains origin.
- Ring wrap, dropped rows, no UTC, selected-row expiry and changing filters preserve proper ordering and state.
- Recording renderer confirms no page-wide clears on age tick or new single event.

Additionally pass the shared [renderer/reception regression matrix](../../RENDERING_AND_ACCEPTANCE.md): inactive updates emit no display calls, overlay blocking, no-signal state, geometry and physical flicker/readability on approved hardware. Record actual build/hardware outcomes separately—this document is a specification, not verification.
