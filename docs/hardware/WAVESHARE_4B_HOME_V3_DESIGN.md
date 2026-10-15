# Waveshare 4B Home v3 — Orc badge toggles navigation

**2026-10-14: design prototype, host-tested geometry only. Not implemented in
production 4B LVGL UI, not visually accepted on a physical device and not flashed.
Supersedes the permanently visible 172 px navigation rail in
`WAVESHARE_4B_HOME_V2_DESIGN.md`.**

## Decision

The OrcSDR badge at the top-left of the shared header is the navigation toggle.
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

**Current deployed Waveshare firmware unchanged by this design proposal.**
