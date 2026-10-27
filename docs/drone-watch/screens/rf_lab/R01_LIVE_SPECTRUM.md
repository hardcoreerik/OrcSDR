# R01 — Live Spectrum

> **Category:** RF LAB · **Implementation gate:** P6 only, after RTL scan/resource arbitration · **Status:** design only, not a currently proven drone signal detector.  
> Parent references: [OrcSDR Drone Watch UI/UX](../../UI_UX_STANDARD.md), [navigation](../../NAVIGATION.md), [data](../../DATA_CONTRACTS.md), [render/test contract](../../RENDERING_AND_ACCEPTANCE.md).

## Purpose: what this screen does

Give RF experimenters an honest narrowband view of the frequency presently received by the existing RTL-SDR, without labeling every spike as a drone or borrowing fictional 2.4/5.8 GHz coverage. This page is a scoped extension of RF Lab, not another DSP pipeline.

## User how-to (press-and-go path)

1. Open RF LAB -> SPECTRUM; verify the receiver owner, exact center frequency, sample rate and whether it is already in another radio mode.
2. Choose a supported frequency profile or request an explicit RECEIVE HERE action; review what existing radio session will be suspended before accepting.
3. View true spectrum samples and level units; tap PEAK INFO for frequency/amplitude of measured bin, not an aircraft classification.

## 1280 × 720 layout and visual hierarchy

Global OrcSDR header y0–113 is owned by shared controls, category bar y649–705, subview selector y118–178; drawable view area x20–1260/y188–634. All rectangles below are **draft reference geometry** calculated from one adaptive layout system; never hand-fork per hardware screen. Maintain 52px-plus touch, minimum comfortable 20–26px row text and main-value 36–56px font target.

- Large spectrum plot x20–920/y218–560, grid and reference scale drawn once; trace bins updated from actual bounded FFT frames.
- Control panel x940–1260: CENTER, SPAN, receiver identity, sample age, settling state, gain and actual level reference, with explicit START/STOP when arbitration supports it.
- Bottom plot legends x20–920: frequency tick labels, measured bandwidth and unavailable frequency edges; top content contains status and one paragraph of coverage caveats.

Follow OrcSDR dark palette, cyan focus and green active status; amber means incomplete/paused/invalid. RF activity never uses a drone icon/color as its asserted classification. Explain colors with a legend, and use line styles or text as well.

## Required real fields and sensor contracts

- FFT bin values, actual samplerate, center_hz, tuner state, config revision, IQ sample counter, dBFS/reference; distinguish calculated spectrum level from calibrated dBm.
- Receiver ownership and arbitration outcome from existing radio service; preserve previous band/audio state.
- Gaps/retune/settle markers and errors/overruns—no plot drawn as current while frames are old or receiver is paused.

The Blog V4 RTL-SDR **cannot directly sample 2.4 or 5.8 GHz**, and simultaneous hopping across channels is impossible with a single receiver. All measurements are within actual tuner/bandwidth, antenna, dwell and active task boundaries. Do not confuse baseband dBFS, radio-reported RSSI dBm and instrument-calibrated absolute power. No inferred aircraft identity from energy.

## Controls and transitions

- TUNE controls delegated to existing frequency/session API and only shown on legal supported dongle ranges; never bypass driver version pin.
- PEAK INFO selects a measured bin and shows center frequency and relative level; no direction/distance estimate.
- OPEN RF LAB links to existing RF Lab dashboard; BACK returns without leaving radio in unintended mode.

Opening this view must not seize the RTL-SDR from another dashboard; any retune/acquisition needs an explicit action and stateful restore path, respecting the existing single radio owner. Global Home/volume/Settings and overlays take precedence.

## Unavailable, paused, degraded and replay states

- No RTL-SDR -> RF RECEIVER UNAVAILABLE; UI can explain but not show generated waterfall.
- Dongle tuned elsewhere -> SPECTRUM PAUSED / RADIO IN USE; explicit action offers switch.
- Transient after retune -> SETTLING, plotted data old/invalidated until new sample counter proves arrival.
- Frequency beyond tuner capability -> OUT OF RANGE rather than extrapolated 5.8 GHz signals.

TEST DATA appears continuously when scripted captures are shown. Unknown observations/gaps must not be drawn as quiet or hidden. Failed source/acquisition never presents green coverage.

## Implementation recipe for Codex/Claude

1. Use the existing spectrum/FFT producer under receiver ownership; subscribe via snapshot only when authorized. Do not create an extra IQ consumer with second USB client.
2. Define bounded spectral frame with monotonically increasing generation, center, span, bin count and revision; drop old generation on retune.
3. Draw grid static once and trace dirty in plot rectangle only. Coalesce FFT rates and avoid fillScreen on every frame.

View model and route in `drone_watch_app`, renderer in bounded RF views under **one** ScreenController owner. Never render from IQ/USB callbacks. Paint static grid/axes on entry, update only changed plot/tick/status regions, clip overlays, and avoid a new full 1280×720 framebuffer unless measured and approved.

### Adaptive 720 × 720 behavior

720 square: full-width spectrum plot about 240px high; one selected center/span card and 52px tuning actions below, with advanced gains in a separate settings tray. Same data/rendering architecture; all controls remain finger-sized.

## Screen acceptance cases

- Source sample counter frozen -> plot clearly stale; request to change unsupported frequency rejected.
- After gain/retune plot data invalidates until actual new frames, no fictitious peaks or current dBFS.
- 100 FFT frames change only plotted region; USB/app IQ drop and audio continuity recorded on physical Tab5.

Also run full shared regression: per-frame plot redraw must not clear the whole screen, no inactive page drawing, no overdraw on tabs/attribution, bounded buffers, verified physical M5GFX readability, and real RF / USB IQ loss counters versus baseline on explicitly authorized hardware. A desktop renderer does not qualify an RF detector.
