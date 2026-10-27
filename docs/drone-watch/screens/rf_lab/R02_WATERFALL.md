# R02 — Waterfall

> **Category:** RF LAB · **Implementation gate:** P6 observed IQ frames only · **Status:** design only, not a currently proven drone signal detector.  
> Parent references: [OrcSDR Drone Watch UI/UX](../../UI_UX_STANDARD.md), [navigation](../../NAVIGATION.md), [data](../../DATA_CONTRACTS.md), [render/test contract](../../RENDERING_AND_ACCEPTANCE.md).

## Purpose: what this screen does

Display RF energy over time as a real receiver waterfall. Its visual appeal must come from real measurements; blank/hatched rows represent monitoring gaps rather than synthesizing continuous occupancy.

## User how-to (press-and-go path)

1. Select RF LAB -> WATERFALL while an authorized spectrum profile is active.
2. Read horizontal frequency, vertical elapsed time and color-relative signal scale; touch a spot for measured frequency/time and dBFS when sample is retained.
3. Pause VIEW (not acquisition) to inspect an old row; resume LIVE without assuming scanning continued during hardware pause.

## 1280 × 720 layout and visual hierarchy

Global OrcSDR header y0–113 is owned by shared controls, category bar y649–705, subview selector y118–178; drawable view area x20–1260/y188–634. All rectangles below are **draft reference geometry** calculated from one adaptive layout system; never hand-fork per hardware screen. Maintain 52px-plus touch, minimum comfortable 20–26px row text and main-value 36–56px font target.

- x20–940 main waterfall y215–584, with drawn frequency ticks and time row labels, bounded RGB565 palette legend.
- x960–1260 side source/status card: profile, center/span, color scale, last received frame, gap count, freeze status and current radio owner.
- One 52px LIVE/PAUSE VIEW action and a palette/scale choice beneath plot; not a miniaturized decorative spectrogram.

Follow OrcSDR dark palette, cyan focus and green active status; amber means incomplete/paused/invalid. RF activity never uses a drone icon/color as its asserted classification. Explain colors with a legend, and use line styles or text as well.

## Required real fields and sensor contracts

- Consecutive real spectral rows with timestamp, tuning parameters and gain/config signature; recorded gap rows and actual tuning changes.
- Actual sample freshness and FFT bin frequency map, derived relative dBFS not raw uncalibrated 'signal strength' as dBm.
- Bounded retained row history and drop counters; reset/raster boundaries explicit when center/span change.

The Blog V4 RTL-SDR **cannot directly sample 2.4 or 5.8 GHz**, and simultaneous hopping across channels is impossible with a single receiver. All measurements are within actual tuner/bandwidth, antenna, dwell and active task boundaries. Do not confuse baseband dBFS, radio-reported RSSI dBm and instrument-calibrated absolute power. No inferred aircraft identity from energy.

## Controls and transitions

- TAP ROW/PIXEL -> timestamp and measured frequency value; no automatic tune unless TUNE HERE explicitly confirmed.
- PAUSE VIEW freezes scroll but can leave authorized acquisition running; state label makes distinction clear.
- SCALE preset applies display-only color transform, not SDR gain. PROFILE -> R04.

Opening this view must not seize the RTL-SDR from another dashboard; any retune/acquisition needs an explicit action and stateful restore path, respecting the existing single radio owner. Global Home/volume/Settings and overlays take precedence.

## Unavailable, paused, degraded and replay states

- No actual rows -> WAITING FOR RF SAMPLES, not random noise art.
- Paused receiver or tuner settle -> distinct hatched GAP bands, never repeats most recent row as current.
- Center frequency changed -> new segment/profile label; do not join two frequency axes as if one sweep.
- Overflow -> LOST ROWS indicator; no silent continuity.

TEST DATA appears continuously when scripted captures are shown. Unknown observations/gaps must not be drawn as quiet or hidden. Failed source/acquisition never presents green coverage.

## Implementation recipe for Codex/Claude

1. Use existing RF Lab waterfall row generator or shared FFT row data; no second FFT unless measured cheaper.
2. Maintain bounded line ring in PSRAM only after capacity measurement; fill one row at a time, map intensity LUT from valid dBFS, draw gap markers explicitly.
3. Push only modified rows/plot scroll area, clip to waterfall viewport, throttle UI redraw if RF is under load.

View model and route in `drone_watch_app`, renderer in bounded RF views under **one** ScreenController owner. Never render from IQ/USB callbacks. Paint static grid/axes on entry, update only changed plot/tick/status regions, clip overlays, and avoid a new full 1280×720 framebuffer unless measured and approved.

### Adaptive 720 × 720 behavior

720 square: plot fills content width, reduced visible time range and 52px LIVE/PAUSE button; side status moved into accessible detail drawer. Same data/rendering architecture; all controls remain finger-sized.

## Screen acceptance cases

- Three real rows + paused gap -> expected pixels and gap legend; center change splits axes.
- Unknown dBm calibration not used on legend; hovering measured bin displays precise unit.
- 100 row ticks update plot only; test on real hardware for SD contention/USB drops and visible flicker.

Also run full shared regression: per-frame plot redraw must not clear the whole screen, no inactive page drawing, no overdraw on tabs/attribution, bounded buffers, verified physical M5GFX readability, and real RF / USB IQ loss counters versus baseline on explicitly authorized hardware. A desktop renderer does not qualify an RF detector.
