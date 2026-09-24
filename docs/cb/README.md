# Tab5 CB scanner dashboard

The CB dashboard is a receive-only, 40-channel AM/USB/LSB receiver and
band-wide scanner. It uses the same top status bar as the other dashboards:
OrcSDR badge, dashboard title, battery, Home, Mute, Visualizer, and Settings.

## How the scanner works

CB streams at 2.4 Msps, so one FFT covers the entire 26.965-27.405 MHz band
from any tuned channel. Every spectrum frame (about 10 per second) the
firmware measures the peak level inside +/-4 kHz of all 40 channels and
compares each one with the band noise floor (the median of the 40 channel
levels). A channel is **active** when it is at least the scan threshold above
that floor, with 3 dB of hysteresis and a 0.7 s release so syllable gaps and
short fades do not end a transmission.

Because all channels are watched at once, the scanner never steps through
quiet channels. It:

1. waits in **SCANNING** until an eligible channel becomes active;
2. retunes the demodulator to it (**LOCKING**, 350 ms) and plays it
   (**RECEIVING**);
3. when the talker unkeys, waits the hang time for a reply (**HANG**);
4. returns to SCANNING.

The priority channel (CH 9 by default, CH 19 or off in SETUP) interrupts any
other channel. When two non-priority channels are busy, the stronger one wins;
the other still appears in the activity bars and log. Audio plays one channel
at a time.

The S-meter and all levels are relative (uncalibrated) receiver measurements.

## Tabs

- **LISTEN:** channel, frequency, mode, clarifier, common-use label, scanner
  state, relative S-meter, and squelch. Controls: `SCAN`/`STOP SCAN`,
  `HOLD`/`RESUME`, `SKIP` (ignore the current carrier until it drops),
  `LOCKOUT`, `CH -`, `CH +`, `CH 9`, `CH 19`, `MODE`, and audio `SQL -`/`SQL +`.
  The lower panel shows live activity bars for all 40 channels with the scan
  threshold line; tap a bar to listen to that channel. Manual tuning while
  scanning holds the scanner on that channel.
- **SPECTRUM:** full-band spectrum and waterfall with channel ticks. Active
  channels are shaded; tap a signal to tune its channel.
- **ACTIVITY:** channels on air now plus a log of the last 48 transmissions
  (channel, length, peak strength above noise, time since). Transmissions
  shorter than 0.25 s are ignored. Tap an entry to tune it.
- **CHANNELS:** the 40-channel scan list. Tap a channel to lock it out or back
  in. `SCAN ALL 40` clears lockouts; `SSB 36-40 ONLY` locks out 1-35.
- **SETUP:** demodulator mode, clarifier (100 Hz steps, +/-1.5 kHz), audio
  squelch (`OPEN` through -35 dBFS), scan threshold (4-30 dB above noise),
  hang time (0-8 s), max hold (unlimited, 15, 30, 60, or 120 s before a stuck
  carrier is skipped), priority channel, and auto sideband.

With **AUTO SIDEBAND** on (the default), tuning a channel selects LSB on
channels 36-40, where sideband operation is customary, and AM on 1-35. `MODE`
still overrides it until the next channel change.

Scanning continues while CB audio plays behind Home, as long as spectrum
graphics are enabled. Opening another band stops it.

## Persistence

These settings are stored in NVS and survive reboots: last manually tuned
channel, mode, clarifier, squelch, scan threshold, hang time, max hold,
priority channel, auto sideband, lockouts, and whether the scanner was running
(it resumes the next time CB opens). Scanner stops are not written to flash.
The activity log is RAM-only.

## Serial control

See `RTL_CB` in [the serial CLI reference](../API_SERIAL_CLI.md#cb-scanner).

The channel table follows the FCC 40-channel plan (CH 23 is out of numeric
order). Channel 19 (27.185 MHz) is the default. CH 9 is reserved for
emergency and traveler assistance; channel labels are conventions only. This
UI does not authorize or implement transmission.

## Retired artwork

`apps/orcsdr-tab5/assets/cb_dashboard_384x470.jpg` and the source under
`docs/cb/dashboard/` belonged to the earlier stylized control panel. The
firmware never loaded them and they are kept only as design reference.
