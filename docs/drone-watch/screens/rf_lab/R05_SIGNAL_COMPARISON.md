# R05 — Signal Comparison

> **Category:** RF LAB · **Implementation gate:** P6 optional measurement analysis; P7 only when compatible receiver qualifies · **Status:** design only, not a currently proven drone signal detector.  
> Parent references: [OrcSDR Drone Watch UI/UX](../../UI_UX_STANDARD.md), [navigation](../../NAVIGATION.md), [data](../../DATA_CONTRACTS.md), [render/test contract](../../RENDERING_AND_ACCEPTANCE.md).

## Purpose: what this screen does

Let a user compare multiple independently measured RF snapshots or sessions with honest units and timestamps. Similar-looking signals do not imply the same transmitter, drone or location.

## User how-to (press-and-go path)

1. Open RF LAB -> SIGNAL COMPARISON; choose two saved/observed RF snapshots with compatible units and acquisition settings.
2. Read overlay or side-by-side relative traces and the exact receiver/profile/time of each capture.
3. Use a DETAILS button to inspect normalization, unmatched bands and receiver gain differences before concluding similarity.

## 1280 × 720 layout and visual hierarchy

Global OrcSDR header y0–113 is owned by shared controls, category bar y649–705, subview selector y118–178; drawable view area x20–1260/y188–634. All rectangles below are **draft reference geometry** calculated from one adaptive layout system; never hand-fork per hardware screen. Maintain 52px-plus touch, minimum comfortable 20–26px row text and main-value 36–56px font target.

- Plot x20–875: two differently styled traces on shared frequency axis only if center/span and units can be aligned; otherwise split two small stacked plots of equal height.
- Inspector x895–1260: A/B source, center/span, dBFS reference, gain, sample age and time gap, with mismatch warning prominently visible.
- Controls SELECT A, SELECT B, NORMALIZE DISPLAY (explicitly labeled cosmetic) and CLEAR within bottom content row.

Follow OrcSDR dark palette, cyan focus and green active status; amber means incomplete/paused/invalid. RF activity never uses a drone icon/color as its asserted classification. Explain colors with a legend, and use line styles or text as well.

## Required real fields and sensor contracts

- Two immutable RF snapshot keys with actual frequency bins, tuner/profile revisions, units, antenna/gain if known, clocks and source IDs.
- Compatibility result: same units/ranges or cannot overlay. Distinct receiver dBFS floors cannot imply calibrated absolute received power.
- Measured time delta and dropped frames; any analytic correlation treated as descriptive, not identification.

The Blog V4 RTL-SDR **cannot directly sample 2.4 or 5.8 GHz**, and simultaneous hopping across channels is impossible with a single receiver. All measurements are within actual tuner/bandwidth, antenna, dwell and active task boundaries. Do not confuse baseband dBFS, radio-reported RSSI dBm and instrument-calibrated absolute power. No inferred aircraft identity from energy.

## Controls and transitions

- SELECT A/B opens bounded recent captures from this session or P5 saved records only; no arbitrary file parsing in UI task.
- OVERLAY toggles only if axes compatible; DIFFERENCE mode demands matched bins and explicit relative units.
- OPEN SOURCE -> S01, OPEN SPECTRUM -> R01 without auto-matching 'drone'.

Opening this view must not seize the RTL-SDR from another dashboard; any retune/acquisition needs an explicit action and stateful restore path, respecting the existing single radio owner. Global Home/volume/Settings and overlays take precedence.

## Unavailable, paused, degraded and replay states

- Only one valid capture -> SELECT SECOND CAPTURE; overlay disabled.
- Different center, sample rate, gain -> DISPLAY COMPARISON LIMITED, explanation not a fabricated match percentage.
- Missing clock -> relative ordering only; missing units -> cannot compute difference.
- Replay source always TEST DATA; any external clip must preserve origin/recorded sample type.

TEST DATA appears continuously when scripted captures are shown. Unknown observations/gaps must not be drawn as quiet or hidden. Failed source/acquisition never presents green coverage.

## Implementation recipe for Codex/Claude

1. Implement pure host-testable trace resampling/axis alignment only when source metadata actually supports it.
2. Keep two finite fixed-length arrays, time-stamped; choose peak/decimation strategy without concealing narrow signals.
3. Render static axes once, repaint trace plot only when A/B selection or style changes; don't touch the live RTL capture.

View model and route in `drone_watch_app`, renderer in bounded RF views under **one** ScreenController owner. Never render from IQ/USB callbacks. Paint static grid/axes on entry, update only changed plot/tick/status regions, clip overlays, and avoid a new full 1280×720 framebuffer unless measured and approved.

### Adaptive 720 × 720 behavior

720 square: compare two stacked compact traces with A/B metadata in a pager; overlay details one action away. Same data/rendering architecture; all controls remain finger-sized.

## Screen acceptance cases

- Two incompatible spectra cannot produce numeric match score or directly subtract mismatched levels.
- Identical recorded frames display stable overlay and metadata, with no auto-claimed transmitter identity.
- Switch A/B repeatedly, test buffer bounds/PSRAM, no full-screen clears on idle live sample progress.

Also run full shared regression: per-frame plot redraw must not clear the whole screen, no inactive page drawing, no overdraw on tabs/attribution, bounded buffers, verified physical M5GFX readability, and real RF / USB IQ loss counters versus baseline on explicitly authorized hardware. A desktop renderer does not qualify an RF detector.
