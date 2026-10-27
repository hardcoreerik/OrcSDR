# A04 — Observed Track History

> **Category:** AIRCRAFT · **Phase dependency:** P4 valid locations / P5 optional saved replay · **Status:** proposed and unimplemented.  
> Shared [UI/UX rules](../../UI_UX_STANDARD.md), [routing](../../NAVIGATION.md), [data contracts](../../DATA_CONTRACTS.md), [rendering/tests](../../RENDERING_AND_ACCEPTANCE.md).

## Purpose and plain-language UX

Illustrate successive **reported** Remote ID positions that were actually captured; gaps are gaps, not interpolated flight paths or complete journey reconstruction.

## User how-to

1. From an aircraft's Details choose TRACK.
2. Browse timestamped received position samples, reported altitude/speed and source changes when available.
3. Tap SHOW ON MAP to see recorded points; inspect a gap or stale update before interpreting movement.

## Tab5 1280 × 720 layout specification

Reference shell is reserved: common OrcSDR header y0–113, 52–60px inner-screen selector y118–178, content-safe rectangle x20–1260/y188–634, five global categories in y649–705. All controls go through the shared layout/hit system, not 25 independent coordinate owners.

- Main x20–850: sparse time-vs-reported-position/altitude sequence or chronological data list; only join segments when continuity rules validated.
- Right x870–1260: selected sample with observed age, claimed lat/lon, altitude datum, receiver ID and origin.
- Explicit GAP/NO COVERAGE legend below plot; horizontal playback scrub/step control only for retained samples.

Typography defaults to large 26–32px headings, 20–26px list rows and 36–56px headline metrics subject to M5GFX font measurement. Use current OrcSDR background/panel/cyan/green/muted palette. Green means actual supported reception, amber means caution/invalid/paused, not threat level. Each active tap region >=52px.

## Field requirements and provenance

- Bounded track_key with exact sequence of valid reported positions, monotonic Rx times, per-sample origin and optional UTC.
- Stream discontinuities, pauses, out-of-order frames and source reset markers; never infer missing points.
- Altitude and speed only if decoded with valid reference; grouping policy bound to source and claimed identity, not track merge magic.

Every reported location, ID, timestamp, altitude or speed remains a *broadcast claim* or a separately identified receiver measurement. Use true valid/absent/invalid states; never synthesize positions or interpret unknown ID as confirmed anonymous aircraft. TEST DATA remains clearly visible in every replay-derived view.

## Tap controls and screen navigation

- Tap a sample -> update detail snapshot and position highlight; MAP -> A05 at selected reported coordinate.
- Select TIMELINE to inspect the raw source event; enable replay scrub only when history retained.
- EXPORT after P5 opt-in -> S03; never offer hypothetical full track file by default.

Global Home/volume/Visualizer/Settings take precedence. Back preserves source/selected observation and scroll when record remains available. The whole suite has one ScreenController owner.

## No-data / unsupported / stale / failure presentation

- Only one valid reported coordinate -> SINGLE OBSERVATION, no path line.
- No position -> NO LOCATION HISTORY, link to A03 decoded field inspector.
- Gap between packets -> discontinuity marks, no straight line presented as observed flight segment.
- Clock unavailable -> show relative sample offsets; expired record remains HISTORICAL not live.

A source cannot show SCANNING merely because chipset and UI exist. Do not silently turn on an RTL tuner or alter C6 firmware. Time since reception uses monotonic clock; UTC only if verified.

## Builder how-to (work units)

1. Use bounded history ring with explicit discontinuity markers and per-source ordering; prepare viewport/polyline geometry outside draw hot path.
2. Only draw connected segments when data and gap rules allow; show each reported point distinctly even if no line drawn.
3. Partially redraw selected-sample highlight and time cursor; avoid scrolling whole framebuffer on each second.

View draws from immutable `DroneWatchSnapshot` on UI task only. Keep received packet parsing, network I/O, storage and map tiles in proper model/adapters/workers. Draw chrome on structural entry only, then invalidate bounded rows/cards and skip unchanged widgets. Avoid a per-screen full-frame framebuffer.

### 720 × 720 adaptive behavior

Prefer timestamped vertical sample list with a SHOW MAP action; chart can be an optional full-width panel, not a miniature side plot. Derive from a common layout solver with touch-safe secondary navigation, not cloned screens.

## Acceptance cases unique to this screen

- Three points with 2-minute capture gap never display continuous solid path; show broken line.
- Repeated packet without position cannot overwrite valid historical sample or resurface as current location.
- No GPS/no UTC still displays sampled order; zoom/select doesn't clear full screen every step.

Also run shared visual regression: 100 value/timer ticks update correct pixels without increasing the full-screen-clear count, off-page state publishes without screen writes, overlay clipping, 1280/720 geometry, and separately recorded physical Tab5 readability and RF continuity. Treat implementation, host build and hardware/RF evidence as distinct outcomes.
