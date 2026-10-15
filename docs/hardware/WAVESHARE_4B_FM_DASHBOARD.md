# Waveshare 4B FM Broadcast — all five tabs

Feature branch: codex/waveshare-4b-fm-dashboard, branched from codex/waveshare-p4-4b-port at fda3984.
Keep local Home v3 hardware qualification independent. Do not merge or flash without physical review.

## References
- Tab5 fm_dashboard.cpp and fm_dashboard.hpp, dashboard_audio_control.cpp and waterfall_style.cpp.
- Original images: docs/images/dashboards/fm-listen.png, fm-spectrum.png, fm-station-rds.png, fm-rf-health.png and fm-settings.png.
- User guide: docs/user-guide/dashboards/fm.md; Wiki: https://github.com/hardcoreerik/OrcSDR/wiki

## Five separate square views
LISTEN: frequency, relative IQ, preset availability, audio/VU unavailable, step/tune controls.
SPECTRUM: real CU8 IQ FFT + waterfall with original FM palette; filter and span must be labeled unavailable until implemented.
STATION / RDS: PS/PI/PTY/RadioText are unavailable; never invent station metadata.
RF HEALTH: only actually measured SDR metrics are populated; missing DSP/audio values are explicitly N/A.
SETTINGS: real working controls can be enabled; gain, audio, scan, record remain disabled until ported.

All five tab labels retain the original order and a persistent bottom tab bar.
Source palette is common FM Tab5 kPanel=0x0841, kCyan=0x2e7f, kGreen=0x6fe8, kMuted=0x8c71, kGrid=0x2945.
The original Tab5 FM dashboard includes actual WFM audio/RDS; the current 4B radio supports only raw IQ at 960 kS/s.
Do NOT call an IQ-only FM visual shell a working FM/audio/RDS decoder.

## Repeatable host test
From the repo root, run:
g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -Iapps/orcsdr-waveshare-4b/main apps/orcsdr-waveshare-4b/tests/fm_4b_layout_host.cpp -o fm-4b-layout-host 2>&1 | tee fm-4b-build.log
./fm-4b-layout-host 2>&1 | tee fm-4b-layout.log
Logs stay beside the commands. Native ESP-IDF 5.5.4 compile is a separate gate, and physical 4B rev1.3 flashes require rev1_x at PSRAM 200MHz.

## Acceptance
- Five separate FM screens and tap routes on physical 720-square display.
- Header, badge and palette visually match original OrcSDR screenshots.
- Live IQ data only, never fake RDS/audio/stereo/driver data.
- Home escape and drawer interactions work; no radio ownership changes.
- Compile, 50-tap test, 5-minute RF streaming regression and owner's visual acceptance.
- Full WFM audio/RDS parity requires a separate DSP/audio integration stage.

## Implemented FM IQ preview (separate feature branch)

- LVGL module: apps/orcsdr-waveshare-4b/main/fm_4b_ui.cpp and .hpp.
- Pure screen/capability model: main/fm_4b_layout.hpp with host tests.
- Home v3 drawer FM entry opens a separate root LVGL FM screen; header H returns Home.
- Orc badge on FM opens its own Home/FM drawer; tapping outside dismisses.
- Five persistent 132x70 tab targets at y636, all separately renderable.
- Live spectrum uses the same 256-bin FFT data as Home, with the exact existing Tab5 FM waterfall_style implementation and palette selection.
- Header displays genuine receiver state and device uptime; missing service readings never pretend to be active.
- FM broadcast tuning restricted to 76-108 MHz while FM preview is active. Input commands still pass to the single existing radio task; SDR driver is unchanged.
- Two large PSRAM RGB565 buffers are allocated once and reused when tabs change. The UI does not own USB Host or DSP.
- No decoded stereo VU, PS, PI, PTY, RadioText, sound, seek/scan, gain setting, presets, recording or FM audio. Those appear explicitly unavailable.

## Suggested dedicated tests

From the repo root:

    g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -Iapps/orcsdr-waveshare-4b/main apps/orcsdr-waveshare-4b/tests/fm_4b_layout_host.cpp -o fm-4b-test 2>&1 | tee fm-4b-test-build.log
    ./fm-4b-test 2>&1 | tee fm-4b-test-run.log

For local ESP-IDF 5.5.4 with the verified 4B P4 silicon rev1.3:

    cd apps/orcsdr-waveshare-4b
    idf.py -B build-4b-fm -D IDF_TARGET=esp32p4 -D SDKCONFIG=build-4b-fm/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.rev1_x" build 2>&1 | tee fm-4b-idf-build.log

Both build and test logs stay in the working directory. The native image must be checked for 200 MHz PSRAM, chip rev1.x, 32MB flash, correct partition, and actual badge/linker setup; never flash a CI default image directly.

Visual device qualification: first boot Home, tap Orc badge, open FM RADIO (IQ only), confirm the five tabs individually and return Home. Verify FM Spectrum shows actual IQ and cannot display stale RF after disconnect. Use 50 taps across all five tabs and Home/FM drawer navigation. RF regression: capture start/enumeration markers and at least five minutes at 960 kS/s with the established powered passthrough, using check_4b_home_log.py. Keep logs in the command directory.

## Real WFM/audio/RDS parity is next, not inferred

Original Tab5 fm_dashboard.hpp::Snapshot provides frequency, DSP filter, span, gain, stereo, RDS carrier/lock, station PS/PI/PTY/RadioText, relative dBFS, left/right audio levels, volume, presets and RF health counters.
Original Tab5 fm_dashboard.hpp::ActionKind supports tune, seek, direct entry, save/scan presets, gain, audio, record, settings, span and filter adjustments.
The current Waveshare radio_task only exposes requested/applied Hz, effective SPS, aggregate drops, streaming state and the live FFT. It has no audio/RDS snapshot producer.

Correct next engineering steps are to extract the tested WFM demodulator, MPX/stereo and RDS/audio paths as reusable core modules, pass truthful state snapshots to the 4B UI, and separately prove USB, CPU/PSRAM and audio-out routing at the required sample rates. Tab5 documentation reports 2.40 MS/s RF acquisition, 240 kS/s FM MPX and 48 kHz audio; the 4B has only qualified a 960 kS/s IQ stream so far. Do not raise RF rate or claim FM audio parity without measurements.

**Branch isolation:** Parent Waveshare Home branch may advance in a local Codex session. Reconcile those changes before integrating this feature branch; do not overwrite local work or merge FM while Home visual acceptance is unresolved.
