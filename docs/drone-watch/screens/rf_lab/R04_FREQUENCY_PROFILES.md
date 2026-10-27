# R04 — Frequency Profiles

> **Category:** RF LAB · **Implementation gate:** P6 with receive-only scan safety gate · **Status:** design only, not a currently proven drone signal detector.  
> Parent references: [OrcSDR Drone Watch UI/UX](../../UI_UX_STANDARD.md), [navigation](../../NAVIGATION.md), [data](../../DATA_CONTRACTS.md), [render/test contract](../../RENDERING_AND_ACCEPTANCE.md).

## Purpose: what this screen does

Offer a few clear RF measurement profiles as presets without advertising them as universal drone frequencies. Profiles define what the currently attached dongle can actually sample; antenna suitability and local regulations vary.

## User how-to (press-and-go path)

1. From RF LAB open FREQUENCY PROFILES; review SIMPLE RECEIVE, MANUAL NARROW SCAN and saved user profile where supported.
2. Tap a profile to read actual center/range, step, dwell, measured tuner limits and whether this dongle supports it.
3. Choose APPLY PROFILE; only then request a receiver retune or scan, with previous mode restored when leaving.

## 1280 × 720 layout and visual hierarchy

Global OrcSDR header y0–113 is owned by shared controls, category bar y649–705, subview selector y118–178; drawable view area x20–1260/y188–634. All rectangles below are **draft reference geometry** calculated from one adaptive layout system; never hand-fork per hardware screen. Maintain 52px-plus touch, minimum comfortable 20–26px row text and main-value 36–56px font target.

- Left x20–730: three or four large profile rows, each named by function not aircraft category, with current/unsupported state.
- Right x750–1260: selected profile editor with center/range/step/dwell, sample rate, expected number of hops and receiver-capability feedback.
- Large APPLY, CANCEL and RESET buttons below; numeric controls use existing OrcSDR frequency keypad/action patterns.

Follow OrcSDR dark palette, cyan focus and green active status; amber means incomplete/paused/invalid. RF activity never uses a drone icon/color as its asserted classification. Explain colors with a legend, and use line styles or text as well.

## Required real fields and sensor contracts

- Saved versioned profile_id, name, device capability, start_hz/end_hz, step_hz, dwell_ms, FFT settings and data provenance.
- Current driver-reported allowed tuning and sample rate, antenna description if user provided (not a measurement).
- Run limits estimated from settings only; actual coverage and observed scan stats belong R03/W04.

The Blog V4 RTL-SDR **cannot directly sample 2.4 or 5.8 GHz**, and simultaneous hopping across channels is impossible with a single receiver. All measurements are within actual tuner/bandwidth, antenna, dwell and active task boundaries. Do not confuse baseband dBFS, radio-reported RSSI dBm and instrument-calibrated absolute power. No inferred aircraft identity from energy.

## Controls and transitions

- SELECT profile populates inspector without retuning; EDIT validates in bounds and prompts for each changed setting.
- APPLY explicitly requests receiver ownership and displays busy/accepted/fail with radio continuity note.
- SAVE user profile only when proven persistent settings; otherwise RAM-only, no fake success.
- OPEN SPECTRUM / OCCUPANCY requires accepted current plan.

Opening this view must not seize the RTL-SDR from another dashboard; any retune/acquisition needs an explicit action and stateful restore path, respecting the existing single radio owner. Global Home/volume/Settings and overlays take precedence.

## Unavailable, paused, degraded and replay states

- Unsupported center/span -> OUT OF RANGE with reason and APPLY disabled.
- No dongle -> VIEW/EDIT allowed but APPLY NOT AVAILABLE; never describe device as connected.
- Profile stale due to dongle change -> NEEDS REVIEW before new scan.
- Bad persistent file/SD full -> retain prior known-good profile and report error.

TEST DATA appears continuously when scripted captures are shown. Unknown observations/gaps must not be drawn as quiet or hidden. Failed source/acquisition never presents green coverage.

## Implementation recipe for Codex/Claude

1. One profile schema and validator in radio policy layer, not handcoded presets in every UI screen.
2. Reuse existing keypad and settings transaction pattern; capture undo/restore state per scan session.
3. Update only edited field widgets and validation text; all storage work asynchronous via existing layer.

View model and route in `drone_watch_app`, renderer in bounded RF views under **one** ScreenController owner. Never render from IQ/USB callbacks. Paint static grid/axes on entry, update only changed plot/tick/status regions, clip overlays, and avoid a new full 1280×720 framebuffer unless measured and approved.

### Adaptive 720 × 720 behavior

720 square: profile chooser first; tap profile to enter full edit sheet. Advanced fields paginate instead of 2-column dense form. Same data/rendering architecture; all controls remain finger-sized.

## Screen acceptance cases

- V4 incapable of 2.4/5.8 GHz profile -> APPLY rejected with truthful tuner span.
- Apply then stop restores exact previous receiver center/mode; invalid step/dwell rejected.
- Failed save cannot leave applied-but-uncommitted 'saved' status; redraw tests for keypad/modal/field edits.

Also run full shared regression: per-frame plot redraw must not clear the whole screen, no inactive page drawing, no overdraw on tabs/attribution, bounded buffers, verified physical M5GFX readability, and real RF / USB IQ loss counters versus baseline on explicitly authorized hardware. A desktop renderer does not qualify an RF detector.
