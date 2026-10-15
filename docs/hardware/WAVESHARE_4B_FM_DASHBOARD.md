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
