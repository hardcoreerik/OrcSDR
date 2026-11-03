# LilyGO Wi-Fi connection follow-up

## Changes

The saved-network action previously requested a disconnect and relied on the
30-second automatic retry. With automatic connection disabled, that action
could not reconnect. It now queues an explicit saved-network connection in the
Wi-Fi owner task, independent of the boot preference. Selecting an already
connected saved network leaves its connection intact.

Submitting credentials or selecting a saved network returns compact Settings
to its connection-status page with immediate Connecting feedback. Queued
requests count as connecting; failed joins show a failure message instead of
Ready. The compact boot toggle reads AT BOOT: ON/OFF.

Home now receives the current IP address and renders a two-column compact
status bar with Wi-Fi connection state/IP and receiver status/clock. This
changes only the status widget when its status revision changes. Legacy Tab5
header rendering is retained.

## Serial parity on the P4 console

All modifying commands below require the existing serial authentication:

| Command | Behavior |
| --- | --- |
| `RTL_WIFI CONNECT_SAVED` | Join the saved network without depending on boot auto-connect |
| `RTL_WIFI BOOT 0` / `RTL_WIFI BOOT 1` | Persist the automatic saved-network connection preference |
| `RTL_WIFI STATUS` | Also reports power, boot preference and join_failed; no passwords |

Power remains a separate setting. Auto-connect does not override Wi-Fi power
OFF. These commands extend the P4 console; they do not claim app-wide serial
parity or multiple saved-network support on that adapter.

## Validation, 2026-11-02

- `python -m pytest tests/test_settings_connectivity_recording.py tests/test_settings_c6_rom_recording.py -q`: 11 passed. Production Settings renderer/input is exercised in both LilyGO orientations, including return to status after a connection action and bounded message redraws.
- r14 native ESP-IDF app build passed; app-only COM32 flash hash verified.
- Saved network joined with boot ON after startup. Boot OFF persisted through
  a soft restart and did not join. Explicit saved-network connection then
  succeeded with boot OFF. Boot ON persisted through another soft restart and
  automatically rejoined.
- A further soft restart captured C6 driver readiness at 8.9 seconds and a
  successful DHCP connection at 13.4 seconds, without a station disconnect
  error in that captured run.
- Actual Settings taps turned boot auto-connect OFF, selected the saved
  network while disconnected, returned to the status page and connected.
  Auto-connect was restored to ON. Home was left visible.
- Native PNGs show connected state and IP on Home in portrait and landscape,
  and connected state/IP plus AT BOOT on Connectivity. Private device images
  remain outside the repository and must not be packaged.
- Receiver status after the boot tests reported streaming at approximately
  2.4 MSps. These tests do not establish uninterrupted audible playback during
  a deliberate restart.

The first join after flashing initially failed and succeeded on automatic
retry; its station disconnect reason was not captured. Subsequent tested soft
restarts joined successfully. This is not proof of universal AP compatibility,
wrong-password diagnostics, router-outage recovery, cold-power-cycle behavior,
or long-duration Wi-Fi/radio coexistence. RSSI still reports zero on this setup
and is not treated as a valid signal measurement.
