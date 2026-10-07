# Smart VFO Home: design and status

Status (2026-10-07): the first stages are built on branch `claude/home-smart-vfo`; see "As built" below for what
shipped and what is still open. The rest of this document is the original design, kept for the reasoning.
Inputs: [discussion 158](https://github.com/hardcoreerik/OrcSDR/discussions/158), a read of the Tab5 Home
code, and a survey of how popular VFO knobs and SDR tuning controls behave.

## As built

Built and verified by host tests and on hardware (Tab5 with OrcDial):

- **Band profiles** (`ui/band_profile.hpp/.cpp`, host-tested): pure data resolving a frequency to a band with its
  usual mode, step list, channel raster (the FM raster starts at 88.1 MHz), filter kind, control style and owner
  dashboard. 78 labelled US bands from 160 m through L-band, named in plain words (FM RADIO, AIR BAND, 70 CM HAM,
  MARINE, AM RADIO...). `Region` is a parameter from the start; only the US plan has data.
- **Per-band steps:** every band has a step list, the chosen step is remembered per band in NVS, and channelized or
  fixed bands show FIXED instead of a dead stepper. BROWSE no longer borrows FM's 100 kHz step.
- **Mode and band are separate:** Home picks the demodulator (NFM, AM, WFM, USB, LSB) while Home is on screen,
  on the general receive, FM, airband, weather and CB bands. AUTO applies each band's usual mode on entering the band;
  a manual choice is pinned per band and remembered. The mode popup explains each mode in plain language.
- **Home controls:** direct frequency entry on a keypad, the band name centered above the spectrum, popups docked at the
  bottom so the spectrum and most of the waterfall stay live, a smoother spectrum (per-pixel trace, display-only
  smoothing, peak-hold dots) that does not touch the receive pipeline, and corrected waterfall hues on every scope.
- **OrcDial Home:** a full-range VFO driven by step counts (the Tab5 owns the step and raster), band name, mode,
  step, span, filter and volume on the round display, a long-press keypad, mode cycling, zoom (span) and filter width
  from the knob with the edge lines shown on the Tab5, a BACK button, and a Dial Settings menu. The band id rides in
  reserved packet byte 7, so the wire version is unchanged.
- **Clock and time zone** (Discussion #126): Settings > System > SET CLOCK, with an optional network sync; see
  `docs/API_SERIAL_CLI.md` ("Hardware clock").

Still open: removing the internal `BROWSE` band value and its console command; Home volume and mute; the Auto-mode undo
note and an AUTO or pinned marker on the mode chip; folding MW, shortwave (the dongle's HF path, so it needs a
per-dongle check), P25, LoRa, ADS-B and pager onto Home's full range; independent filter edges (a DSP change);
band files on the SD card with a cache on the Dial; regions other than the US; custom gesture mapping; memories and
band stack; a setup-wizard step for region, location and clock.

## As built

Built and verified by host tests and on hardware (Tab5 with OrcDial):

- **Band profiles** (`ui/band_profile.hpp/.cpp`, host-tested): pure data resolving a frequency to a band with its
  usual mode, step list, channel raster (the FM raster starts at 88.1 MHz), filter kind, control style and owner
  dashboard. 78 labelled US bands from 160 m through L-band, named in plain words (FM RADIO, AIR BAND, 70 CM HAM,
  MARINE, AM RADIO...). `Region` is a parameter from the start; only the US plan has data.
- **Per-band steps:** every band has a step list, the chosen step is remembered per band in NVS, and channelized or
  fixed bands show FIXED instead of a dead stepper. BROWSE no longer borrows FM's 100 kHz step.
- **Mode and band are separate:** Home picks the demodulator (NFM, AM, WFM, USB, LSB) while Home is on screen,
  on the general receive, FM, airband, weather and CB bands. AUTO applies each band's usual mode on entering the band;
  a manual choice is pinned per band and remembered. The mode popup explains each mode in plain language.
- **Home controls:** direct frequency entry on a keypad, the band name centered above the spectrum, popups docked at the
  bottom so the spectrum and most of the waterfall stay live, a smoother spectrum (per-pixel trace, display-only
  smoothing, peak-hold dots) that does not touch the receive pipeline, and corrected waterfall hues on every scope.
- **OrcDial Home:** a full-range VFO driven by step counts (the Tab5 owns the step and raster), band name, mode,
  step, span, filter and volume on the round display, a long-press keypad, mode cycling, zoom (span) and filter width
  from the knob with the edge lines shown on the Tab5, a BACK button, and a Dial Settings menu. The band id rides in
  reserved packet byte 7, so the wire version is unchanged.
- **Clock and time zone** (Discussion #126): Settings > System > SET CLOCK, with an optional network sync; see
  `docs/API_SERIAL_CLI.md` ("Hardware clock").

Still open: removing the internal `BROWSE` band value and its console command; Home volume and mute; the Auto-mode undo
note and an AUTO or pinned marker on the mode chip; folding MW, shortwave (the dongle's HF path, so it needs a
per-dongle check), P25, LoRa, ADS-B and pager onto Home's full range; independent filter edges (a DSP change);
band files on the SD card with a cache on the Dial; regions other than the US; custom gesture mapping; memories and
band stack; a setup-wizard step for region, location and clock.

## Goal

Home becomes a general-purpose **Smart VFO** that is band-aware, on both the Tab5 and the OrcDial:

- Home stays **context-aware**: coming back from FM, Airband, Shortwave and the rest carries the radio state.
- You can go to any frequency (type it, step it, spin it) and Home suggests a sensible **mode, bandwidth and
  step** for where it is in the spectrum. Suggestions, not restrictions: an experienced user can override.
- Home may *offer* the matching dashboard ("OPEN AIRBAND") but never forces a switch.
- Split of responsibilities (from the discussion): **Home** = easy general-purpose VFO, **dedicated dashboards** =
  guided purpose-built experiences, **RF Lab** = full manual workbench.
- The OrcDial Home mirrors the same state and offers the same controls. The Tab5 stays authoritative.

## What Home was before this work (from the code)

- Home has no VFO of its own. It displays whatever band the last dashboard left active
  (`rtl_ui_band`, `rtl_ui_frequency_hz`); the mode label is the band name (`BROWSE`, `WX`...).
  See `home_dashboard_snapshot()` in `ui/main.cpp`.
- A tune on Home retunes **within the current band**; nothing resolves the band from the frequency.
- `BROWSE` already spans the dongle's whole range, but its mode is fixed at NFM and the Home MODE chip is
  display-only. `home::ActionKind` has no mode, step-set or squelch action.
- Step size: the Home snapshot falls back to 12.5 kHz for BROWSE, WX, Airband, ADS-B and POCSAG, and
  `step_size_up/down` only handles FM, AM and shortwave. BROWSE stepping uses FM's step
  (`rtl_step_frequency()`), which explains the 100 kHz jumps reported in the discussion.
- There is no frequency entry on Home. `freq_keypad` exists but is used only by the FM, AM and shortwave dashboards.
- `kRfBandGuide` (22 entries, US only) maps ranges to a band, label and preset but only drives a label today.
  Entries overlap (P25 spans 136 to 941 MHz), so lookup order matters.
- `filter_standards` is a good template: pure data, no display or RTOS, host-tested.
- To verify before relying on it: `home_filter_kind()` returns the airband AM filter for BROWSE at 118 to
  137 MHz while the mode string says NFM. Which demodulator actually runs there is unconfirmed.

## Proposed design

### 1. Band resolver (pure data, host-tested)
A new module in the style of `filter_standards`: given a frequency, region and dongle capability, return a
**band profile**:

| Field | Meaning |
|---|---|
| id, name | e.g. `fm_broadcast`, "FM Broadcast", "Airband voice", "2 m amateur" |
| default mode, allowed modes | WFM, NFM, AM, USB, LSB, CW |
| step list, default step, raster (spacing and origin) | e.g. FM 200 or 100 kHz on a 100 kHz origin, MW 10 or 9 kHz, airband 25 or 8.33 kHz |
| filter kind | links to `filter_standards` |
| control style | free-tune or channelized; which parameters apply (squelch, bandwidth, none) |
| owner dashboard | the dashboard to offer ("OPEN AIRBAND") |

- Seeded from `kRfBandGuide`, made region-aware (US first, a region setting later) and user-extensible by file.
- Per-band **remembered** step and mode (the discussion asks for per-context step, not one global value).
- Dongle-aware: report the tunable range per dongle profile, and flag bands a given dongle cannot reach.

### 2. Home actions
Add `mode_set`/`mode_next`, `step_set`, `squelch`, `open_matching_dashboard`, and direct frequency entry on Home
(reusing `freq_keypad`). Fix the step-size handler so every band has a step. Direct volume and mute on Home
(requested in the discussion).

### 3. OrcDial Home
The Tab5 sends a small **band context** with the radio state: band id and name, mode, step and step list,
control style, a short label (station or channel), and the owner dashboard. The Dial renders by band class and
never carries its own band table. It sends the same semantic actions the Tab5 Home accepts.

### 4. Knob behaviour (informed by the survey)
- Rotate tunes by the band's step with **velocity acceleration**: a dead zone at slow speed, a start threshold,
  a velocity multiplier, capped so it never crosses a band edge or overshoots a raster.
- **Digit tuning** by touch: tap a digit, rotate to change that digit.
- Press cycles focus. Double press = quick step. Long press = a menu of the band's parameters. Press-and-rotate
  is a second layer. The main knob returns to tuning on its own.
- **Band stack**: three memories per band, recalled by band.
- **VFO lock** as an on-screen toggle, since the Dial is handheld.
- Tune commands are **coalesced** (net delta every 50 to 100 ms); the display moves at once and shows pending
  until the Tab5 confirms.

### 5. Custom mapping (later)
A layered profile (global, band class, specific band, memory) maps gestures to semantic actions, stored as
plain data on the SD card and sent to the Dial on connect. Bounded in size, no code execution, and unable to map
to unsafe actions.

## Reported issues versus this plan (discussion 158)

| Report | Addressed by | Status |
|---|---|---|
| STEP SIZE arrows do nothing on WX and BROWSE | Per-band step for every band (stage 1). WX is channelized, so its stepper should be hidden or disabled instead. | Fixed by stage 1 |
| Step not honoured, 444.150 to 444.250 | Cause found: BROWSE falls back to FM's step. Per-band defaults fix it (stage 1). | Fixed by stage 1 |
| MODE cannot be changed; shows last dashboard's mode or BROWSE | `mode_set` and `mode_next` on Home (stage 2). | Fixed only for modes the DSP can run; see risk below |
| Bandwidth always 25 kHz | Filter presets follow the band profile and mode. Not yet verified why it shows 25 kHz. | Likely fixed with mode selection |
| Wants an agnostic screen: enter frequency, mode, listen; clicking the spectrum to reach 422 or 444 MHz is slow | Direct frequency entry and mode on Home (stage 2). | Fixed by stage 2 |
| Wants direct volume and mute without the top-right icon | Volume and mute controls on Home (layout change). | Fixed by stage 2 |
| Step remembered per context; US MW 10 kHz; FM 200 kHz but staying on odd 100 kHz | Per-band remembered step and defaults. The FM raster needs an **origin offset** (88.1 + n x 200 kHz), so the band profile must carry raster origin as well as spacing. | Fixed by stage 1, with that addition |
| Where to send ideas without opening many issues | Not a code question. A single ideas thread or tracking issue would answer it. | Unanswered in the thread |
| Preference for a separate expert dashboard instead of an expert Home | Not adopted: Home stays simple by default and advanced controls are opt-in; RF Lab remains the workbench. | Design disagreement, keep an eye on it |

**Main risk.** The demodulator is chosen by the band (`RtlBand`), not by a free mode setting. Letting Home pick
AM, NFM, WFM or SSB at an arbitrary frequency means either mapping a mode onto the right existing pipeline or
generalising the pipeline. Today SSB exists only in the CB path and WFM only in the FM path. Stage 2 has to
resolve this before the mode chip can truly work everywhere.

## Staged plan (each stage is independently testable)

1. **Step fix + band resolver.** Pure-data resolver with host tests; per-band step for every band. Closes the
   original report in discussion 158 without redesigning Home.
2. **Home mode selection and frequency entry** on the Tab5.
3. **Band context in the OrcDial protocol**; Dial Home renders and controls it.
4. Memories, band stack and scan.
5. Profile-driven mapping and an editor.
6. Identification overlays (station and channel names, signal type) over time.

## Decisions (2026-10-06)

- **Auto mode** changes the mode only when you cross into a different band, never on small tunes inside one. Where a
  band is ambiguous (for example 2 m amateur can be FM or SSB) Auto only suggests. A short note with an undo follows
  every automatic change. A manual choice sticks (pinned) and is remembered per band.
- **All modes selectable anywhere** (AM, NFM, WFM, USB, LSB; CW later), which means separating the band (UI context) from
  the demodulation chain (mode). Home Auto first works on the modes that already exist; the pipeline refactor follows.
- **OrcDial Home waveform** is decorative by default, with an option to scale it with signal strength.
- **Band knowledge** lives in files on the Tab5 SD card with a built-in fallback, with a cached copy on the OrcDial.
- **Mode names:** the OrcDial shows the short form on its chips ("AUTO · WFM") and the long form on tap. On the Tab5 Home
  the plain-language description needs a home: the gain mode appears twice at the bottom (the SMART/AGC chip and the
  "GAIN SMART" footer line), so the footer line is the candidate slot. Location still to be confirmed against a screenshot.

## Regions: plan for all of them from the start

Today only LoRa has a region table. Everything else assumes the US: the band guide (`kRfBandGuide`), the CB channel plan,
the NOAA weather frequency, the AM broadcast profile (10 kHz step; 9 kHz elsewhere), the airband catalog and the FM
defaults. A region has to be a first-class setting before the band resolver is built.

- **Region model:** an ITU region (1, 2 or 3) plus an optional country code, with a user override. Resolution order: the
  user's choice, then the receiver location (country), then a generic world profile.
- **Band plan files:** one data file per region, with a fallback chain of country, ITU region, world. Each entry carries the
  range, mode, step and raster (spacing and origin), filter kind, channel plan, owning dashboard and a confidence flag
  (high means Auto may switch, low means suggest only). A built-in US profile stays compiled in so a missing SD card still
  works. Files are shareable and user-editable.
- **Things that differ by region** (to carry in the data, not in code): FM step and band edges (200 or 100 kHz; Japan uses
  76 to 95 MHz), AM step (10 or 9 kHz), airband spacing (25 or 8.33 kHz), amateur band edges per ITU region, CB and
  license-free personal radio (US CB, FRS and GMRS versus CEPT CB, PMR446 and LPD433), marine channel plans, weather
  radio (NOAA in the US only), public-safety bands, LoRa and other ISM plans.
- **Universal:** ADS-B 1090 MHz, GPS and satellite downlink bands, the dongle's tunable range.
- **Where region is set:** the startup wizard and Settings both write the same value (location first, region derived from
  it, override allowed). The map pack follows the location: the existing receiver-location record already has a `map_pack`
  field, and open PRs #83 and #84 are the map work (a world coastline basemap and a denser Lane County pack).
- **OrcDial:** receives the resolved band context from the Tab5 and caches it, so the Dial itself needs no region logic.

## Open questions

- Where does the startup wizard live? No wizard exists on `main`, in an open PR, or in the docs; confirm the branch or spec.
- Confirm the Tab5 Home location for the mode description against a screenshot of the status area.
- Which regions get real band-plan data first, beyond the US profile?
- Which demodulators can Home offer outside the CB and FM paths before the pipeline refactor?

## References (VFO survey)

IC-7300 notes and firmware notes, Icom band stacking, Elecraft KX3 manual and VFO lock note, FlexControl quick
start and acceleration thread, Contour ShuttlePro guide, Griffin PowerMate as a VFO knob, Gqrx mouse and
keyboard options, SDRplay forum threads on SDRuno tuning, the SDR# users guide, the Tecsun PL-880 fine-tuning
bug write-up, and an EEVblog thread on encoder acceleration. Behaviours were taken from search summaries and
vendor pages; exact details should be checked against the manuals before any is copied.
