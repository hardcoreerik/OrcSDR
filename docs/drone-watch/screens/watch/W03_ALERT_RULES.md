# W03 — ALERT RULES

> **Category:** WATCH · **Delivery gate:** P5 opt-in deterministic alerts after real source/event gates · **Status:** proposed screen specification (no runtime implementation).  
> **Shared authority:** [UI contract](../../UI_UX_STANDARD.md) · [navigation](../../NAVIGATION.md) · [source model](../../DATA_CONTRACTS.md) · [rendering acceptance](../../RENDERING_AND_ACCEPTANCE.md).

## Purpose and default experience

Allow users to create a small number of understandable alerts based on verified observations or source health; absolutely no threat estimation, distance-from-RSSI warning or silent alarm defaults.

## User how-to

1. Open WATCH -> ALERT RULES; see preset suggestions in OFF state and plain explanations.
2. Enable FIRST RID OBSERVATION, KNOWN BROADCAST RETURNS, SOURCE OFFLINE or SUSTAINED RF ACTIVITY only when their underlying event source is available.
3. Test rule with visibly marked TEST DATA, set notification option and save; any save error leaves the old setting intact.

## Exact Tab5 composition: 1280 × 720 reference

Shared OrcSDR header y0–113; secondary page selection y118–178; content safe area x20–1260/y188–634; bottom five-category bar y649–705. The following are **reference regions**, calculated through one common layout solver rather than hard-coded in each view:

- Left x20–805: 3–4 high-touch rule rows with ON/OFF, concise trigger summary and source qualification; no tiny multi-field rule-builder grid.
- Right x830–1260: selected rule criteria, exact event kind, cooldown, whether source is available, recent real activations and HELP.
- Action tray bottom-right: TEST RULE, SAVE, RESTORE DEFAULTS; Save stays disabled if unsaved criteria invalid.

Use OrcSDR colors (black background, muted panel, cyan focus, green actual receiving, amber degraded); readable rows 20–26 px, headings 26–32 px, big status 36–56 px. Touch targets minimum 52px. Retain existing Home/volume/Visualizer/Settings/battery header controls.

### Actual displayed fields and dependencies

- Rule id/name, enabled, input event type, source selector, exact predicate parameters, cooldown and last-trigger monotonic timestamp.
- Separate alert delivery mode (on-screen/sound if permitted) from actual receiver, user notification permission and current volume/mute.
- Counter and history derived from fired rules, never from forecasts or external untrusted ID reputations.

## Controls, tap behavior and transitions

- Tap rule -> edit bounded options with defaults; no arbitrary script entry or user-defined executable predicates.
- Toggle enabled must immediately show pending then saved/failed, and disable when required source unsupported.
- TEST RULE produces a banner TEST DATA / TEST ALERT; CLEAR HISTORY requires confirmation; disable alert sound independently from receiver sound.

## Empty, disabled, stale and unreliable states

- No sources -> rule explains WAITING FOR RECEIVER; it isn't secretly satisfied.
- Source paused -> alert from silence must not trigger 'no drone' event; source offline can trigger health alert.
- Clock unavailable -> cooldown uses monotonic time; no last-fired UTC guess.
- Storage failure -> NOT SAVED; don't change persistent rule state on disk failure.

All source origin, aircraft identity, location, mode, timestamps and RSSI are evidence-bound. `TEST DATA` always visible during fixtures/replay. No `DRONE CONFIRMED`, `NO DRONES`, unverified pilot location, invented threat probability or continuous scanning claim. Source updates must not draw after losing screen ownership.

## Builder how-to and ownership

1. Create versioned bounded AlertRule model with deterministic evaluator consuming typed event stream and source states.
2. Use existing NVS/Settings persistence protocol and prompt feedback; no network requests, plugin or remote notification requirement.
3. Render only changed switch/criterion fields and alert activity rows; no heavyweight draw from callback.

Implement through `drone_watch_app` snapshot and WATCH view renderer under **one** `screens::Id::drone_watch`; route actions via [NAVIGATION.md](../../NAVIGATION.md). Never reimplement USB/RTL-SDR/C6/SD ownership in this page; UI cannot perform blocking network/storage tasks. Structural paint once on entry, value-only changes via dirty rectangles, no full-screen clear on timers. **Do not allocate a new 1280×720 framebuffer simply to draw this page.**

### Adaptive 720 × 720 rendering

List one rule per row and navigate to full-page editor; 52px toggles and explicit SAVE/CANCEL controls. Preserve text meaning and state, not exact pixel coordinates; secondary page picker becomes Prev/Next or an accessible selector. No touch hit overlaps.

## Screen-specific acceptance tests

- No rule fires from simulated replay in live mode; TEST RULE is marked across notification and history.
- Two rapid events obey cooldown; source paused/offline logic correct; malformed packet never matches RID rule.
- Failed save rolls back enabled flag and UI; 100 rule evaluations produce no unsolicited full-screen clears.

Additionally pass the shared [renderer/reception regression matrix](../../RENDERING_AND_ACCEPTANCE.md): inactive updates emit no display calls, overlay blocking, no-signal state, geometry and physical flicker/readability on approved hardware. Record actual build/hardware outcomes separately—this document is a specification, not verification.
