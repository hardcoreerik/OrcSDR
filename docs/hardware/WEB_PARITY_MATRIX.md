# Web page parity with the OrcSDR Home (read-only audit, 2026-11-02)

The goal of the web work is a one-to-one reproduction of the OrcSDR dashboards as web pages. This is the first measurement of how close the Home screen is. **Nothing in this file changes any behaviour**; it only records what is and is not reachable, so each gap can be decided before anything is wired.

**Parity target: the latest `origin/main` of OrcSDR, never a version tag.** Compared against `origin/main` **`cdc8181a`** (2026-11-02). The matrix was first written against the `v0.5.0-beta.1` tag; that was only a starting point. Re-verified against `cdc8181a` on 2026-11-02: `apps/orcsdr-tab5/ui/home_dashboard.hpp` (the 36 Home actions), `home_vfo.hpp/.cpp` and `band_profile.hpp` are byte-identical on this branch and on `cdc8181a`, so rows 1-15 below hold for Home. Everything else (the other dashboards, 344 commits of changes on `origin/main` not in this branch) is **not yet re-verified**. Name the commit you compare against whenever this matrix is updated.

Sources read: `apps/orcsdr-tab5/ui/home_dashboard.hpp` (the 36 Home actions), `apps/orcsdr-p4-core/main/home_app.cpp` (what the P4 console's own Home does with them) and the web page's `app.js` (now `apps/orcsdr-web/web/app/app.js`, imported from OrcSDR-Headless `feat/m5gfx-web-mirror` at `67e535e`; see `apps/orcsdr-web/PROVENANCE.md`). What each Tab5 action does on the Tab5 itself was not re-read from the Tab5 firmware here; the column says what the shared Home header declares.

Legend: **OrcSDR** = the page calls an OrcSDR route that reaches the real receiver/Home; **page-only** = changes only what the browser shows; **toast** = the page says it is not available; **UI** = handled inside the compiled M5GFX UI, identical on every build.

| # | Tab5 Home action (v0.5.0-beta.1) | P4 console Home (native) | Web page today | Gap |
|---|---|---|---|---|
| 1 | `tune_frequency` | handled (`retune`) | OrcSDR (`receivers/tune`, or `fm?action=start` while OrcSDR's audio session owns the receiver) | none |
| 2 | `step_up`, `step_down` | handled (band-aware step) | OrcSDR via `tuneTo` | none |
| 3 | `span_up`, `span_down` | handled | OrcSDR (`receivers/rate`, pinned; or `fm?action=start&sps=`) | none |
| 4 | `gain_auto`, `gain_tenth_db`, `rtl_agc` | handled | OrcSDR (`tuner/gain`, `tuner/agc`) | none |
| 5 | `volume_up`, `volume_down`, `sound_toggle` | handled: the board's speaker | page-only: the browser's own volume (needs an audio session; otherwise a toast) | **intentional deviation**: owner wants browser audio never to change the board's volume or mute |
| 6 | `step_size_up`, `step_size_down` | handled (`cycle_step_hz`, per band) | page-only (OrcSDR's Home step is not changed) | **gap**: needs a Home action on OrcSDR and the page calling it |
| 7 | `mode_set` (0 AUTO, 1 NFM, 2 AM, 3 WFM, 4 USB, 5 LSB) | handled (pins the demodulator for the band) | OrcSDR (`POST /api/v1/home?action=mode_set`; a browser audio session follows live), since 2026-11-03 | none for the pick; the page keeps only the pin it set itself, per band, until state sync is decided |
| 8 | `filter_set` (bandwidth Hz) | handled | AM screen: channel mode in the browser; elsewhere: toast / shown only | **gap** |
| 9 | `filter_standard` | not in `home_app.cpp` | toast: "not applied by OrcSDR yet" | **gap** (not implemented on the P4 console either) |
| 10 | `filter_edges` | not in `home_app.cpp` | page-only (draws the two filter lines) | likely UI-only; confirm on the Tab5 |
| 11 | `open_clock` | not in `home_app.cpp` | no case: "Action not wired: open_clock" | **gap** (clock settings; OrcSDR has the clock/UTC offset setting) |
| 12 | `open_device_settings` | handled (opens Settings) | UI (`_m5_open_settings`) | check that Settings is the same screen |
| 13 | `open_dashboard` | handled | UI list; Weather, Marine and Satellite tune Home to a band; other tiles toast "not in this build yet" | **gap** per dashboard (a separate audit per screen) |
| 14 | `waterfall_contrast_down/up`, `waterfall_palette_next`, `waterfall_speed_next` | UI | UI | none |
| 15 | `gain_open/close`, `filter_open/close`, `mode_open/close`, `keypad_open`, `open_browser`, `close_browser`, `browser_previous/next` | UI | UI | none |

## What OrcSDR already has that the page does not use

`home_app.cpp` already applies `mode_set`, `step_size`, `filter_set` and the rest on the real Home. The web page cannot reach them because there is no route that injects a Home action, and the page (`app.js`, other repo) answers with a toast instead of calling one.

## Proposed closure (needs the owner's decision before any code)

1. **OrcSDR side, an addition only:** one signed route, `home?action=<name>&value=<n>`, that hands the same `home::Action` to `home_app` that a touch would. No existing screen changes.
2. **OrcSDR status:** report Home's step, mode (already in `/api/v1/freq`), bandwidth and span so the page can show the real state instead of its own.
3. **Page side (this changes how the page's buttons behave, so it is the owner's call):** make rows 6, 7, 8, 9 and 11 call that route. Either as a patch to `apps/orcsdr-web/web/app/app.js`, or as a script injected the way the login script is.
4. A per-screen matrix for FM, AM, CB, Airband, Shortwave, P25, ADS-B, ACARS, LoRa, POCSAG, RF Lab and the rest, after Home is decided.

## Evidence level

- Action list, P4 handlers and page cases: read from the sources named above (not run).
- Browser behaviour of tune, step and FM start: exercised on COM29 with the built-in browser (tune 96.1 -> 96.225 MHz, step 12.5 -> 25 kHz, FM start from the FM screen); mode, filter and step-size buttons were not clicked.

## Update: OrcSDR side built (2026-11-02, addition only)

- `POST /api/v1/home?action=<name>&value=<n>` (signed): hands the same `home::Action` a touch would reach to Home's UI task. Allowed: `step_up`, `step_down`, `step_size_up`, `step_size_down`, `span_up`, `span_down`, `mode_set` (0-5), `filter_set` (Hz, 0 = the band's standard). **Refused** with `not_allowed_from_the_web`: `volume_up`, `volume_down`, `sound_toggle` (the board's speaker), `open_dashboard`, `open_device_settings`, `open_clock` (the on-device screen). Unknown names are `unknown_action`.
- `GET /api/v1/home`: Home's own VFO state (frequency, applied frequency, band, mode, step and whether it is adjustable, span, filter bandwidth, mode choice/active/suggested, mode_selectable).
- `tools/web_home_test.js` (24 checks) passes on COM29: step size, mode pin, filter override, span, the refusals, unsigned refusal, and the board's volume and tuned frequency unchanged.
- **The web page itself is unchanged**: rows 6, 7, 8, 9 and 11 still behave as listed above until the owner decides how the page should call the route (patch to `apps/orcsdr-web/web/app/app.js`, or an injected script).
