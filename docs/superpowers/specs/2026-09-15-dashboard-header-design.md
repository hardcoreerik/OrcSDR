# Canonical Dashboard Header

## Goal

Give every Tab5 dashboard the same top-bar geometry and controls so new
dashboards cannot accidentally overlap navigation, audio, battery, or Settings
controls.

## Shared contract

`dashboard_audio_control` remains the shared owner of the right-side controls.
It will expose one header renderer and one hit-test path for:

- the OrcSDR badge at one fixed size and position;
- Home;
- a single speaker control that opens the existing volume tray and controls
  mute/volume;
- Visualizer;
- Settings; and
- battery state.

Dashboard modules supply only their title and middle status content. The shared
control region is reserved and dashboard-specific drawing must not enter it.
The duplicate speaker/volume indicator beside the battery is removed.

## Dashboard migration

AM, FM, Shortwave, ADS-B, P25, POCSAG, LoRa, RF24, and Home use the same shared
badge and right-side coordinates. Existing dashboard-specific content remains
in the middle region. Shortwave retains its standalone module and replaces the
legacy Browse navigation overlay with its own frequency keypad.

Settings uses the same right-side audio controls. Its Close button moves into
the title area, outside the reserved control region.

Home keeps Wi-Fi and RTL-SDR state but centers clock/date and gives battery its
own non-overlapping cell.

## Verification

The existing UI self-check gains one geometry assertion covering badge,
status, battery, Close, and control hit regions. Validation is the targeted
self-check, native ESP-IDF build, flash, serial boot/health evidence, and a
separate physical review of alignment and touch behavior.

The existing 2.4 MS/s pipeline and dashboard-specific content are otherwise
unchanged.
