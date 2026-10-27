# R03 — Band Occupancy

> **Category:** RF LAB · **Implementation gate:** P6 experimental scan scheduling after receiver arbitration · **Status:** design only, not a currently proven drone signal detector.  
> Parent references: [OrcSDR Drone Watch UI/UX](../../UI_UX_STANDARD.md), [navigation](../../NAVIGATION.md), [data](../../DATA_CONTRACTS.md), [render/test contract](../../RENDERING_AND_ACCEPTANCE.md).

## Purpose: what this screen does

Measure which sampled frequencies were occupied during explicitly logged dwell windows. The page must make it impossible to mistake sampled windows for continuous full-band surveillance or to infer a drone type solely from occupancy.

## User how-to (press-and-go path)

1. Select BAND OCCUPANCY; pick only frequencies within the connected RTL-SDR's tested tuning range.
2. Review the exact scan plan and effect on live radio/audio before pressing START SCAN.
3. Inspect each frequency bucket's measured busy fraction, dwell time and missed/gap samples; STOP at any time.

## 1280 × 720 layout and visual hierarchy

Global OrcSDR header y0–113 is owned by shared controls, category bar y649–705, subview selector y118–178; drawable view area x20–1260/y188–634. All rectangles below are **draft reference geometry** calculated from one adaptive layout system; never hand-fork per hardware screen. Maintain 52px-plus touch, minimum comfortable 20–26px row text and main-value 36–56px font target.

- x20–900 histogram/heatmap area: horizontal frequency buckets with actual sampled dwell coverage and an explicit UNKNOWN pattern for never-visited buckets.
- x920–1260 scan controls: plan name, range, step, dwell, cycles completed, current/paused state and explicit START/STOP.
- Lower content legend differentiates MEASURED OCCUPIED, MEASURED QUIET, UNOBSERVED, SETTLING, RECEIVER ERROR. No generic green coverage claim.

Follow OrcSDR dark palette, cyan focus and green active status; amber means incomplete/paused/invalid. RF activity never uses a drone icon/color as its asserted classification. Explain colors with a legend, and use line styles or text as well.

## Required real fields and sensor contracts

- Actual frequency, range and tuner capabilities; samplerate/FFT bin width, sensitivity rule and per-hop time/sample count.
- Visited dwell windows with measured power relative to a validated local baseline; detector threshold and its units.
- Unobserved intervals, settle time and scan duty fraction computed only from measured scheduling—not assumed simultaneous channels.

The Blog V4 RTL-SDR **cannot directly sample 2.4 or 5.8 GHz**, and simultaneous hopping across channels is impossible with a single receiver. All measurements are within actual tuner/bandwidth, antenna, dwell and active task boundaries. Do not confuse baseband dBFS, radio-reported RSSI dBm and instrument-calibrated absolute power. No inferred aircraft identity from energy.

## Controls and transitions

- SELECT PROFILE -> R04 to define bounded scan candidates; START SCAN requires explicit radio ownership arbitration.
- STOP -> restore prior tuner/band exactly, with status confirmed; TAP BUCKET -> R01 tuned only after explicit follow-up confirmation.
- EXPORT OCCUPANCY after P5 session logging exists, labeled RF ACTIVITY not DRONE COUNT.

Opening this view must not seize the RTL-SDR from another dashboard; any retune/acquisition needs an explicit action and stateful restore path, respecting the existing single radio owner. Global Home/volume/Settings and overlays take precedence.

## Unavailable, paused, degraded and replay states

- No receiver or radio busy -> PLAN UNAVAILABLE / RADIO IN USE with reason.
- Sweep incomplete -> partial bars and UNOBSERVED lanes, no interpolated busy percentages.
- Frequency outside RTL-SDR span -> excluded by validation, not marked quiet.
- Retune/settling samples -> excluded from percent calculations and counted as gaps.

TEST DATA appears continuously when scripted captures are shown. Unknown observations/gaps must not be drawn as quiet or hidden. Failed source/acquisition never presents green coverage.

## Implementation recipe for Codex/Claude

1. Reuse one existing receiver-session scheduler, settling rules and FFT acquisition path; no independent hidden background tuner.
2. Build finite scan plan with bounded max hops, dwell, tuning capability and cancel/restore transition.
3. Compute occupancy only from recorded valid samples; draw changed bar for current bucket, not full heatmap every dwell.

View model and route in `drone_watch_app`, renderer in bounded RF views under **one** ScreenController owner. Never render from IQ/USB callbacks. Paint static grid/axes on entry, update only changed plot/tick/status regions, clip overlays, and avoid a new full 1280×720 framebuffer unless measured and approved.

### Adaptive 720 × 720 behavior

720 square: show a 5–8 bucket paged bar chart and a scan control card; advanced range/step fields move to PROFILE editor. Same data/rendering architecture; all controls remain finger-sized.

## Screen acceptance cases

- Known 2-frequency fixture with one busy one quiet yields two distinct measured bars and remaining unknown.
- Pause and aborted run excluded from denominator; source unplug restores status without claiming valid measurements.
- Runtime measurement under FM/ADS-B/LoRa regression and 100 iterations of plot updates without full-screen clears.

Also run full shared regression: per-frame plot redraw must not clear the whole screen, no inactive page drawing, no overdraw on tabs/attribution, bounded buffers, verified physical M5GFX readability, and real RF / USB IQ loss counters versus baseline on explicitly authorized hardware. A desktop renderer does not qualify an RF detector.
