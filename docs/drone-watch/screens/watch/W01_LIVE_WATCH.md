# W01 — LIVE WATCH

> **Category:** WATCH · **Delivery gate:** P3 replay UI; P4 verified live receive · **Status:** W01 M5GFX shell, Home integration, Sources/Help and unavailable-state model **IMPLEMENTED + HOST-TESTED** on 2026-10-27. Live C6/OrcNode Remote ID reception, valid aircraft list, native build and physical 5-inch acceptance are **NOT IMPLEMENTED / NOT RUN**. W01 Sources now has a **read-only, host-tested C6 Hosted/HCI capability diagnostic**; it does not enable Bluetooth or prove reception (see [P2a self-review](../../evidence/2026-10-28-p1-p2a-self-review-and-ble4-ingress.md)). [Host checkpoint](../../evidence/2026-10-27-w01-live-watch-host-checkpoint.md).  
> **Shared authority:** [UI contract](../../UI_UX_STANDARD.md) · [navigation](../../NAVIGATION.md) · [source model](../../DATA_CONTRACTS.md) · [rendering acceptance](../../RENDERING_AND_ACCEPTANCE.md).

## Purpose and default experience

Make Drone Watch genuinely press-and-go: show whether at least one verified acquisition method is listening, the newest qualifying reception, and an obvious path to details. First entry works with zero hardware and does not imply clear airspace.

## User how-to

1. Open Drone Watch from OrcSDR Home; land on LIVE WATCH without selecting a receiver or radio frequency.
2. Read the prominent acquisition banner: SCANNING BLE, NO RID OBSERVED, SCANNER PAUSED (reason), SOURCE UNAVAILABLE, or TEST DATA.
3. Tap the latest observed broadcast to see Aircraft Details, or SOURCES to understand missing/paused equipment.

## Exact Tab5 composition: 1280 × 720 reference

Shared OrcSDR header y0–113; secondary page selection y118–178; content safe area x20–1260/y188–634; bottom five-category bar y649–705. The following are **reference regions**, calculated through one common layout solver rather than hard-coded in each view:

- Main left column x20–845: reception headline and most recent Remote ID broadcast card, identifier shown only when valid; large time-since-received and acquisition method.
- Right inspector x865–1260: source health/active methods, last message's validated fields and small help link; never a second tiny graph.
- Bottom part of content x20–1260: 1–3 most recent event rows/mini timeline, with a VIEW TIMELINE control; body area remains above y634.

Use OrcSDR colors (black background, muted panel, cyan focus, green actual receiving, amber degraded); readable rows 20–26 px, headings 26–32 px, big status 36–56 px. Touch targets minimum 52px. Retain existing Home/volume/Visualizer/Settings/battery header controls.

### Actual displayed fields and dependencies

- Primary SourceStatus for every enabled acquisition adapter; supported methods, active receiving intervals, last packet and pause reason.
- Latest valid OpenDroneID observation reference with source_id, transport, monotonic arrival, parse status, reported ID validity and optional claimed location; no raw location lookup required to show list.
- Scoped event counts: actual decoded messages observed and distinct records under documented grouping policy; don't label event counts as number of drones.

## Controls, tap behavior and transitions

- Tap event card -> A02 Aircraft Details (selected observation key); malformed packet -> A03 Inspector only if retained.
- Tap source status -> S01 Sources focused on source_id; VIEW TIMELINE -> W02.
- Tap in-page i -> explanation of what monitoring does and does not cover. Opening page never starts a dangerous tuner retune.

## Empty, disabled, stale and unreliable states

- Source absent: big SOURCE UNAVAILABLE and one CONNECT / SOURCES action; show no made-up sample.
- Source active, zero valid RID: NO RID OBSERVED, with explicit listening mode; no 'all clear'.
- C6 cannot capture selected protocol: METHOD UNSUPPORTED; Wi-Fi/SDIO pause: SCANNER PAUSED and gap interval.
- Stale latest record: keep entry only as LAST OBSERVED with increasing age, fade/remove under policy, never as current detection.

All source origin, aircraft identity, location, mode, timestamps and RSSI are evidence-bound. `TEST DATA` always visible during fixtures/replay. No `DRONE CONFIRMED`, `NO DRONES`, unverified pilot location, invented threat probability or continuous scanning claim. Source updates must not draw after losing screen ownership.

## Builder how-to and ownership

1. Define a fixed-size LiveWatchSnapshot composed only by drone_watch_app on the UI task from the source/event model.
2. Render static banner/cards/nav once; isolate status label, last-seen timer, event text and counters into bounded dirty rectangles.
3. Bind button actions through shared route dispatcher; cap displayed recent rows and coalesce high-rate source notifications.

Implement through `drone_watch_app` snapshot and WATCH view renderer under **one** `screens::Id::drone_watch`; route actions via [NAVIGATION.md](../../NAVIGATION.md). Never reimplement USB/RTL-SDR/C6/SD ownership in this page; UI cannot perform blocking network/storage tasks. Structural paint once on entry, value-only changes via dirty rectangles, no full-screen clear on timers. **Do not allocate a new 1280×720 framebuffer simply to draw this page.**

### Adaptive 720 × 720 rendering

At 720×720 present reception headline, latest event and two large DETAILS/SOURCES buttons vertically; replace right inspector with expandable DETAILS view. Preserve text meaning and state, not exact pixel coordinates; secondary page picker becomes Prev/Next or an accessible selector. No touch hit overlaps.

## Screen-specific acceptance tests

- No receiver vs receiving with zero packets generate visibly different banners.
- A malformed RID update cannot create valid aircraft status; a later source pause instantly changes coverage text.
- After 100 second ticks and 100 packet increments, full-screen-clear counter is unchanged; touching global Home/volume still works.

Additionally pass the shared [renderer/reception regression matrix](../../RENDERING_AND_ACCEPTANCE.md): inactive updates emit no display calls, overlay blocking, no-signal state, geometry and physical flicker/readability on approved hardware. Record actual build/hardware outcomes separately—this document is a specification, not verification.
