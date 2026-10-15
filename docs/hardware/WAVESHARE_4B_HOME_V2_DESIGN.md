# Home 720×720 v2 — superseded always-visible-nav proposal

**SUPERSEDED by [Home v3 Orc badge drawer](WAVESHARE_4B_HOME_V3_DESIGN.md).** This earlier layout retained a permanent navigation rail; the user instead chose a badge-toggled overlay so the spectrum and waterfall can occupy the full width.

**Status: DESIGN PROPOSAL. Not integrated into the Waveshare firmware. Not approved or flashed.**
This proposal responds to the owner's failed visual-parity review of the original
Waveshare 4B Home build. Current flashed IQ prototype remains unchanged.

## Primary visual reference (actual Wiki screenshot)

- [OrcSDR Wiki Home](https://github.com/hardcoreerik/OrcSDR/wiki)
- [Actual 1280×720 Home screenshot](https://github.com/hardcoreerik/OrcSDR/wiki/images/v0.3.0-beta.1/home.png)
- [Gain panel screenshot](https://github.com/hardcoreerik/OrcSDR/wiki/images/v0.3.0-beta.1/home-gain.png)
- [Wiki Screenshot Catalog](https://github.com/hardcoreerik/OrcSDR/wiki/Screenshot-Catalog).
  Published catalog records 94 captures from a 2026-09-29 Tab5 session,
  **78 linked publicly**, 16 retained locally for provenance/privacy.
  Captures were sourced from a special PNG-capable build on main `897c60a`,
  not necessarily the current shipping build.
- Working Tab5 source: `apps/orcsdr-tab5/ui/home_dashboard.cpp`,
  `dashboard_audio_control.cpp`, `waterfall_style.cpp`.

The Wiki is reachable by following the repository's Wiki link; the previous
gallery doc understated Wiki availability and omitted the complete catalog.

## Goals of the single-screen redesign

Retain **recognizable OrcSDR**, not a new product. The visually compact 720
square Home keeps the source Orc badge, OrcSDR wordmark, HOME subtitle,
status semantic groups, nav rail with LAST USED/ALL DASHBOARDS, spectrum and
waterfall, centered frequency, tune/step/span, popover access to gain/mode/
filter/palette/settings, and the persistent RTL-SDR health strip.

The geometry is **purpose-built**, not a global 1280→720 scalar transform.
Host-testable coordinates are in
`apps/orcsdr-waveshare-4b/main/home_v2_layout.hpp` and tests in
`apps/orcsdr-waveshare-4b/tests/home_v2_layout_host.cpp`.

| Region | Native pixels | Content and interaction |
|---|---|---|
| Top header | y10–77 | 64×67 badge slot, OrcSDR HOME, Wi-Fi/SDR/time, home/mute/settings |
| Recent nav | x14–186, y88–667 | LAST USED, 7 entries, ALL DASHBOARDS |
| Spectrum | x197–706, y88–273 | 485×142 real plot, tuner marker and no-data state |
| Waterfall | x197–706, y282–467 | 485×141 live plot, classic palette/speed |
| Frequency | x197–706, y476–549 | large frequency/touch keypad, mode and filter state |
| Controls | x197–706, y559–666 | two rows: span/step/tune -/+, gain/bias/palette/more |
| Status footer | x14–706, y675–711 | RTL-SDR / sample rate / BW / gain / bias / level |

**Trade-off for owner review:** left rail stays visible but narrows to 172px.
Spectrum and waterfall narrow to 485px; five or more controls move into
secondary drawers/panels. Full current Tab5 Home behavioral parity is NOT
achieved solely by this proposal. The action ledger will map every popup,
scanning control and tap route before implementation.

## Color discipline

`TFT_BLACK` backdrop; Home frame/plot tokens `kCyan=0x05FF`,
`kGreen=0x6FE0`, `kDim=0x4228`, `kPanel=0x0021`; the common header
values include `0x2E7F`, `0x6FE8`, `0x0841` in the Tab5 source.
Waterfall must use the exact existing `waterfall_style` Classic default and
7 selectable palettes. Do not replace it with generic heatmap code.

## Preview caveats

The local concept preview renders **no synthetic RF trace or waterfall**;
the frequency `152.007 MHz` is a static value copied from the Wiki's
original Home screenshot, **not** a live receiver reading. The drawn badge
face in the preview is an illustrative stand-in only because the original
binary badge could not be materialized into the sandbox; **production must
use `apps/orcsdr-tab5/main/orc_badge_104.rgb565` unmodified**.
Preview font and icon glyphs are not final—they must be reconciled against
the actual Tab5 theme assets. Do not approve final visual parity based only
on the placeholder artwork.

The preview is meant to obtain high-level owner approval on structure,
spatial proportions and navigation. Actual device screenshot comparisons
remain mandatory after original asset integration.

## Reproduce geometry host test

From OrcSDR repo root (no ESP-IDF required):

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic \
  -Iapps/orcsdr-waveshare-4b/main \
  apps/orcsdr-waveshare-4b/tests/home_v2_layout_host.cpp \
  -o home-v2-layout-host
./home-v2-layout-host 2>&1 | tee home-v2-layout-host.log
```

Save the log in the working directory. The geometry test passes in a local
sandbox with the current proposed rectangles; it does not test physical
touch mapping, LVGL rendering, actual Orc badge, USB or RF streaming.

## Approval gates

- [x] Locate and inspect actual Wiki Home capture; review source.
- [x] Define Home-specific 720x720 geometry and host-test.
- [x] Produce preview layout for owner design discussion (sandbox only).
- [ ] Owner approves general placement, nav, status grouping and density.
- [ ] Include original Orc badge, original Tab5 font/icon assets where feasible.
- [ ] Implement LVGL renderer behind existing 4B build switch / alternate
      module without changing radio task or Tab5.
- [ ] Native compile and screenshot device; verify touch and legibility.
- [ ] Reuse `waterfall_style`, no mock data on hardware.
- [ ] Independently qualify externally powered USB Host and IQ.
- [ ] Only then consider another dashboard.

**Never auto-flash this preview.**
