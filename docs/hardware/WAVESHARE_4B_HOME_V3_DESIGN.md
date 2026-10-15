# Waveshare 4B Home v3 — Orc badge toggles navigation

**2026-10-14: Home v3 LVGL source integrated on the development branch, with
host-tested geometry. Not visually accepted on a physical device or deployed.
Supersedes the permanently visible 172 px navigation rail in
`WAVESHARE_4B_HOME_V2_DESIGN.md`.**

## Decision

The OrcSDR badge at the top-left of the shared header is the navigation toggle.
The adjacent OrcSDR/HOME header is also a full 155x67 toggle target and shows
`MENU >` while closed, `MENU <` while open, making navigation discoverable.
Start with drawer CLOSED on every boot and screen entry. Badge tap toggles
OPEN/CLOSED. While open, touch outside the drawer in the covered *content area*
closes it without passing the tap through to tuning controls; selecting a
**supported** dashboard route or switching screens closes it. The badge and
header remain accessible in either state. The drawer is a nonblocking UI
overlay; RF IQ capture/FFT/waterfall continue underneath without resizing,
reallocating plots or relinquishing USB ownership.

The user requested this behavior specifically to preserve full-width spectrum
and waterfall views without permanently sacrificing horizontal space.

## Original OrcSDR theme authority

- [Actual OrcSDR Wiki Home screenshot](https://github.com/hardcoreerik/OrcSDR/wiki/images/v0.3.0-beta.1/home.png), actual Tab5 1280x720 capture.
- `apps/orcsdr-tab5/ui/dashboard_audio_control.cpp` (badge/header);
  `apps/orcsdr-tab5/ui/home_dashboard.cpp` (Home behavior);
  `apps/orcsdr-tab5/ui/waterfall_style.cpp` (seven exact palettes).
- Original badge: `apps/orcsdr-tab5/main/orc_badge_104.rgb565`.
  Do **not** ship the simplified placeholder Orc face from the concept art.
- Source Home cyan: RGB565 `0x05FF`, green `0x6FE0`, dim `0x4228`,
  panel `0x0021`; shared headers may use `0x2E7F`, `0x6FE8`, `0x0841`.
  Preserve source colors, font hierarchy, branded controls and the
  Wi-Fi / RTL-SDR / time *semantic* status groups. No fake battery or
  connectivity. Exact status/title values reflect hardware availability.

## Square geometry

The portable, constexpr layout lives at
`apps/orcsdr-waveshare-4b/main/home_v3_drawer_layout.hpp`.

| Region | Coordinates (x, y, w, h) |
|---|---|
| Badge tap target | (14, 10, 64, 67) |
| Status header | (246, 10, 250, 67) |
| Spectrum plot, menu closed | (26, 119, 668, 142) |
| Waterfall plot, menu closed | (26, 314, 668, 141) |
| Frequency and keypad region | (14, 476, 692, 73) |
| Two-row tuner controls | (14, 559, 692, 107) |
| Persistent RF-health footer | (14, 674, 692, 37) |
| Navigation drawer (when open) | (14, 88, 286, 578) |
| Dismissal scrim (when open) | (300, 88, 406, 578) |

Plots gain **183 px (37.7%)** of width from the v2 visible-nav proposal:
668 versus 485 pixels. This is layout capacity, not increased RF bandwidth
or FFT resolution. The footer, header and badge remain uncovered by drawer.

### Menu contents

Use the actual Tab5 Wiki ordering for the Home screenshot:
HOME, P25 RADIO, AM RADIO, SETTINGS, RF LAB, FM RADIO, ADS-B,
ALL DASHBOARDS. `LAST USED` must eventually be driven by the shared
recent-history dashboard model, not hardcoded sample order.

Never make inactive menu entries look like working navigations. Waveshare
currently has NO ported feature dashboards. A route can be shown with a
disabled/coming-soon state until its individual screen is physically
validated. In a future full port, navigation will use the shared
`dashboard_registry` and `navigation_service` contracts.

### Touch, focus, and asynchronous activity

- Minimum 44x44 physical pixel targets on this 720x720 UI design; badge 64x67.
- Event capture: menu overlay consumes content taps so the underlying
  spectrum, tuning, gain and keypad cannot receive accidental touches.
- Dismiss the menu by badge tap, scrim tap, successful selection or screen
  transition. Do not use a touch-gesture that conflicts with waterfall tuning.
- Optional 150-200 ms drawer animation may be added in the LVGL presentation
  task, but must not block receiver/task processing or slow FFT work.
- Live status/data can continue updating offscreen; don't repaint full screens
  unnecessarily or manufacture RF waterfall content.
- Header `VOL`, `VIS`, Settings, mode and filter are visually reserved
  but must be marked inactive until functionality exists. The mockup is
  deliberately not behavioral parity with the original Tab5 Home.

## What was generated in the authoring sandbox

Two precisely 720x720 PNG/SVG renderings: `orcsdr_home_v3_closed` and
`orcsdr_home_v3_open`, plus a side-by-side image. **These are conceptual
mockups** with vector badge/font/icon placeholders, no real RF plot, no
simulated connection, and no claim to be screenshots from the board.
Asset-provenance requirement remains open for native integration.

## Repeatable host qualification

From the root of the OrcSDR checkout, in a g++17-capable shell:

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic \
  -Iapps/orcsdr-waveshare-4b/main \
  apps/orcsdr-waveshare-4b/tests/home_v3_drawer_host.cpp \
  -o home-v3-drawer-host \
  2>&1 | tee home-v3-build.log
./home-v3-drawer-host 2>&1 | tee home-v3-test.log
```

Keep logs in the working directory. This tests rect bounds, 668px
plot widths, menu placement, >=44px targets, badge toggle, dismissal,
screen change, backdrop pointer exclusivity and footer/header reachability.
It is NOT a firmware compile, display render verification, radio IQ test or
physical touch/USB acceptance.

## Integration gates (still open)

- [x] Real Wiki screenshot verified against Tab5 Home code.
- [x] Full-width collapsed / overlay drawer design rendered.
- [x] v3 geometry and nav-state tests written and locally passed.
- [ ] Owner approves the v3 visual direction and content density.
- [ ] Integrate original badge (not vector substitute) and exact glyph assets.
- [ ] Port a shared theme/header abstraction and LVGL drawer event model.
- [ ] Screenshot capture on actual P4 4B, compare palette/typography/hit targets.
- [ ] Restore missing original Home popups/actions without fictitious live data.
- [ ] Obtain independent radio/USB qualification and sustained capture gate.

**The earlier physical IQ baseline uses Home v1; it does not qualify Home v3.**

## LVGL implementation (2026-10-14)

- The new board-specific renderer in apps/orcsdr-waveshare-4b/main/home_v3_ui.cpp builds actual OrcSDR Home v3 rectangles and toggled navigation. It is NOT yet physically tested or accepted.
- CMake embeds the ORIGINAL apps/orcsdr-tab5/main/orc_badge_104.rgb565; renderer converts sampled pixels to native LVGL RGB565. Never substitute a generated Orc logo.
- CMake also compiles the ORIGINAL apps/orcsdr-tab5/ui/waterfall_style.cpp directly. Home uses CLASSIC and cycles the seven source palettes.
- The main.cpp radio task is retained with the original v0.9.3 driver, 960 kS/s and 6x16KiB transfer settings. Only UI-facing glue is replaced.
- Live implemented UI actions: badge menu toggle/dismiss, HOME dismiss, frequency STEP, TUNE -/+, PALETTE. Remaining controls and other dashboards are shown as unavailable, not impersonated.
- No fake IQ plot on device; any retained plot is cleared when receiver leaves state RECEIVING IQ.
- Independent gates still open: CI/native compile, original badge pixel endianness on screen, touch hitboxes, display render/frame rate, 5-minute RF regression, owner visual acceptance.
- Do not flash or merge to main without an explicit review of this new firmware build.

## Local preflight of fda39842, 2026-10-14

- Badge copy is byte-identical to the Tab5 source asset (SHA-256
  `638f26c0242ebc5eda5cb41b987a08d7627ea77a321173150b25ad07aa1d359d`).
  Initial review incorrectly inferred big-endian storage from the Tab5
  display-specific byte swap. The asset is little-endian; see the physical
  correction below. Physical color acceptance is open.
- Original Tab5 `waterfall_style.cpp` is a direct CMake source, with Home's
  CLASSIC default and all seven source palettes. No new palette implementation.
- Creation occurs under the BSP LVGL lock; updates run in the LVGL timer.
  Radio data crosses the existing snapshot/critical-section boundary. Drawer
  visibility changes allocate no canvases or plots; drawer and dismissal
  objects are created above content, with the badge hit target accessible.
  Static review does not prove actual event routing on the touchscreen.
- Corrected STEP title/value overlap (title y=2..22, value y=25..42), retained
  all six fractional MHz digits so 12.5 kHz tuning is not truncated, and fixed
  the stale Home v1 boot label. STEP and PALETTE actions now log their values.
- Existing Home FFT/tuning, v2 geometry and v3 drawer host tests all passed
  using g++17 with `-O2 -Wall -Wextra -Werror -pedantic`; five strict-log
  validator unit tests passed. No physical hitbox or visual approval claimed.
- COM40 CH343 serial identity `5B91033266` re-probed as P4 v1.3,
  MAC `e8:f6:0a:e4:43:02`, 32 MB flash. Probe reset back to existing firmware.
  Original power-passthrough arrangement is to be retained for the RF test.
- Fresh native ESP-IDF 5.5.4 rev1.x build passed: app 857,376 bytes, 18%
  partition space free, 200 MHz PSRAM configuration. Image checks and ELF
  badge/shared-palette symbols verified; full hashes and source patch identity
  are recorded in `WAVESHARE_4B_SCREEN_GATES.md`. No physical v3 acceptance.

## First physical v3 boot and RF gate, 2026-10-14

- Owner-approved COM40 flash of the verified image succeeded; serial confirms
  v3 initialization, 200 MHz PSRAM memory-test pass and GT911 detection.
- Full startup capture identifies `blog_v4l_r828s`, includes RF START and
  310.11 seconds of IQ telemetry. Unmodified strict checker passed, exit 0:
  min post-warmup 959,604 SPS, zero added drops. Original stationary streaming
  baseline is preserved. Full image/log identities are in the screen ledger.
- No drawer/tuning/palette touch events in this timed capture. Physical badge
  colors/typography, closed/open photos, 50 interactions, outside-tap exclusivity,
  navigation under IQ load and owner visual acceptance remain open. Do not
  promote this radio result to visual or full Home acceptance.

## Physical badge correction, 2026-10-14

- Owner reports drawer interactions work correctly, but badge colors are
  wrong. Owner photo shows the pink/red badge, PLASMA waterfall, and live IQ
  with zero drops. Drawer behavior is owner-reported, not serial-counted;
  no separate memory-leak measurement is available.
- Decoding the unchanged original asset as big-endian reproduces that wrong
  badge; little-endian produces the correct green Orc. The Tab5 display byte
  swap must not be copied as an LVGL asset-storage rule. Corrected only the
  badge pixel assembly to little-endian; source asset/palettes remain unchanged.
- Local inspection renders `badge-big-endian-inspection.png` and
  `badge-little-endian-inspection.png` are binary-format diagnostics, not new
  artwork or device screenshots. Rebuild and approved device flash still
  required to verify the correction physically. **Home requires changes**
  until the corrected badge is observed and owner-approved.
- Incremental rebuild passed with the little-endian correction; app remains
  857,376 bytes and valid for P4 v1.0–1.99. New image SHA-256 and preservation
  details are recorded in the screen ledger. Corrected image not flashed yet.
