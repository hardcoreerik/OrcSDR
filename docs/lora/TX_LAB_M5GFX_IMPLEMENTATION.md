# OrcSDR LoRa TX Lab — native M5GFX UI integration (phase 1)

**Development branch:** `codex/lilygo-lora-tx-ui-m5gfx-20261102`
**Based on:** `claude/lilygo-tdisplay-p4-amoled` at `fe75c4d98ecd4450b3bc3b5e25710efded24bd18`
**Status:** Native C++ rendering and input integration, with recording-host tests.
**Important:** No SX1262 SPI backend or TX is enabled; the LilyGO AMOLED display backend and device framebuffer capture remain separate bring-up gates. No merge into the LilyGO branch or origin/main has been performed.

## Goal

Use **OrcSDR's actual shared M5GFX dashboard drawing path**, not an independently authored HTML, raster illustration or rebranded design concept. Preserve the established LoRa UI:

- RGB565 black background, `kPanel = 0x0841`, cyan `0x2e7f`, green `0x6fe8`, yellow `0xff24`, muted `0x8c71`, grid `0x2945`.
- Standard 1280x720 header, real badge drawn by `audio_header::draw_brand`, bottom tabs and RX views on landscape.
- A dedicated 568x1232 portrait TX view with **real** `orcsdr::badge::draw` asset and independent touch hit rectangles; do not downscale 1280x720 content into the narrow panel.
- No invented RX measurements, TX success history, RF output levels or transmitter authorization.
- TX remains locked unless the **device-side** `lora_tx_policy.hpp` authorization gate passes. Hardware readiness stays profile-only on the LilyGO in this phase.

## Implemented

### Native code

- `apps/orcsdr-tab5/ui/lora_tx_views.hpp`: C++ drawing templates that call real M5GFX methods (`setTextDatum`, `drawString`, `fillRoundRect`, `drawRoundRect`, `drawFastHLine`, etc.). Three modes:
  1. **Operator** — SX1262 packet settings, RF path/region gate, deliberate ARM and bounded bench-test send (the current hardware policy blocks both).
  2. **Messages** — packet composer and receive-activity surface. Message editing and TX history are expressly unavailable until data sources exist. No misleading enabled SEND.
  3. **Dual Radio** — clearly separated RTL-SDR IQ monitor and SX1262 TX sections. IQ plot is a reserved grid, not a fabricated spectrum. RX and TX values come from different snapshot sources.
- `apps/orcsdr-tab5/ui/lora_dashboard.cpp`: integrates those modes into the existing TX Lab tab, preserves RX-only five-tab layout, supports mode switching, applies touch-gate checks and renders portrait header/content/footer using the actual OrcSDR badge asset.
- `apps/orcsdr-p4-hosyond-console/main/screen_lora.cpp`: unchanged RX path, still reports documented-only onboard SX1262 and refuses all TX action requests; this work does **not** adjust RF hardware or its safety policy.
- Source-of-truth TX state remains `Snapshot.tx` and `lora_tx_policy.hpp`. No hard-coded "TX READY", pretend antenna path, fake packet log or auto-arming behavior.

### Layout and interaction

- **Landscape:** 1280x720 logical space. Header 0–119, content starts at 138, bottom TX-inclusive navigation y640–719. Existing RX-only boards retain five tabs; a declared onboard SX1262 adds TX Lab as tab six.
- **Portrait:** 568x1232 logical space. Dedicated TX header 0–139, mode selector y155–210, content y225–1125, status footer y1145–1231. Top HOME returns to Home. Native portrait RX, map and other dashboards are **not** ported by this UI slice.
- **Mode switch:** user tap repaints the screen as a structural transition. Ordinary TX state updates repaint bounded widgets; the TX view has no forced idle full-frame clears.
- **Safety:** UI arm/send events are emitted only after local policy checks. The future driver must independently enforce TX security and timeouts. The current P4 adapter refuses every TX action.

## How to run and test the host suite

On a checkout of this feature branch, on Linux with g++ (C++17):

```bash
git switch codex/lilygo-lora-tx-ui-m5gfx-20261102
bash tools/test-lora-tx-ui.sh
```

The script creates:

```
build/lora-tx-ui/layout
build/lora-tx-ui/layout.log
build/lora-tx-ui/policy
build/lora-tx-ui/policy.log
build/lora-tx-ui/integrated-syntax.log
build/lora-tx-ui/result.log
```

The first program runs the real `lora_tx_views.hpp` drawing routines against the repository M5GFX recording stub for both 1280x720 and 568x1232 and every mode. It checks bounds, header/footer exclusion and fail-closed control labels.

The second program checks `lora_tx_policy.hpp` separately.

A third executable, `lora_tx_simulation_tests.cpp`, checks all **960**
combinations of five SX1262 readiness levels, 32 admission bitfields,
three screens and two orientations. It also scans thousands of touch points
for control overlap and tests per-view partial repaint bounds.

The integrated dashboard executable links the real `lora_dashboard.cpp` against
neutral host substitutes. It tests RX-only Tab5 navigation, TX UI actions,
the lack of idle full-screen redraws, and fail-closed **hot-unplug / reattach**
behavior in both orientations. Device reattachment resets the TX Lab to its
Operator page; no prior enabled control or stale page is resumed. Hardware signal events are never invented; the tests
generate only host snapshots with explicit provenance. The integrated dashboard file is syntax-checked through a focused host M5Unified stub to detect C++ integration errors. These tests do **not** drive a physical panel, verify M5GFX font metrics, prove pixel-level accuracy or transmit RF.

CI: `.github/workflows/lora-tx-ui-tests.yml` runs on this branch and uploads the host log files. It runs both normal C++ host tests and an additional **ASan + UBSan** pass:

```bash
ORCSDR_LORA_ASAN=1 bash tools/test-lora-tx-ui.sh
```

The workflow copies baseline logs to `build/lora-tx-ui-normal/` before sanitizer execution and uploads both sets. The first full green sanitizer run was [GitHub Actions #41526815917](https://github.com/hardcoreerik/OrcSDR/actions/runs/41526815917) for commit `9e06a446ed819b79e3922d19334ed865e9f44a74`. This verifies host-visible draw paths, not physical panel performance or RF behavior.

### Native display capture after LilyGO bring-up

The existing screen-capture procedure is documented in `tools/capture_landing_dashboards.py` and `docs/UI_RENDERING_STANDARD.md`. It uses firmware UI documentation commands and framebuffer capture to produce actual 1280x720 Tab5 screenshots. On the LilyGO, **first implement/verify its RM69A10/GT9895 native backend**; then establish an analogous native 568x1232 capture path and compare screenshot text/geometry against the actual user-visible panel.

Do not label host screenshots as "real M5GFX captures" and do not claim the portrait view is operating on the LilyGO until the panel and touch path are measured. The existing 800x480 Hosyond console's M5GFX compatibility layer cannot, by itself, validate a 568x1232 AMOLED touchscreen.

## Critical P4 portrait integration note

The P4 `ScaledGfx::width()` and `height()` return the **logical design grid**
(default 1280x720), not the physical target panel size. The LilyGO 568x1232
panel must be initialized with `ScaledDisplay::begin(target_width,
target_height, 568, 1232)` **after** a real AMOLED backend has been added.
Simply connecting a portrait panel and calling the current two-argument
`begin()` will leave the firmware in the landscape UI.

This must be coordinated with native portrait Home and touch mapping; it is
not safe to switch design dimensions globally and assume the rest of the
1280x720 dashboards already fit. The UI tests explicitly reject arbitrary
portrait-like sizes. A missing SX1262 profile now shows a safe portrait
no-radio page, and an SX1262 disappearing at runtime removes TX controls
instead of leaving a stale armed-looking display.

The UI gate is advisory. The physical adapter and future network API must
enforce their own current-device authorization; the host suite does not
certify the SX1262 hardware, its antenna switch, regional profile, power
limit, or actual TX waveform.

## Known next steps (not silently considered complete)

1. Render native device captures with actual embedded font and Orc badge; compare against `docs/images/dashboards/lora-overview.png`, `lora-traffic.png`, and `lora-rf-health.png` for visual consistency.
2. Add a native portrait RX navigation/content experience, or keep the TX Lab entry explicit as currently scoped.
3. Add a real SX1262 driver and independently verified packet RX on the LilyGO. The TX buttons stay disabled until conducted RF test evidence supports a driver-provided TX-ready state.
4. Replace packet composer placeholder with bounded draft text model, selected destination and source-bound send path; no raw web input can bypass hardware TX admission.
5. Connect the dual-radio IQ graph to actual RTL-SDR data with measured transport, preserve stream continuity and do not invent spectra.
6. Expose the same radio state/action model in OrcSDR Web after independent auth/pairing rules are designed.
7. Test RTL RX-only regression on Tab5, mode-switch touch mapping, pixel/contrast and readability on real 4.1-inch LilyGO, and absence of redraw flicker at device load.

## Future merge

Merge **this feature branch** into `claude/lilygo-tdisplay-p4-amoled` only after reviewing the diff, running targeted tests, and reconciling any concurrent LoRa dashboard edits. Do not merge into `origin/main` as a side effect. Changes here should remain view-level and must not rewrite radio drivers or the LilyGO board profile.
