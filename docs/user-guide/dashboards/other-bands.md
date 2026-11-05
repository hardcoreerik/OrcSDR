# AM, WX, CB, and shared receiver routes

The current application includes dedicated AM, CB, Shortwave Explorer and
Airband receiver screens. Shortwave's AM/USB/LSB controls share the common
receiver/session and DSP infrastructure rather than using a separate driver.
Weather uses its configured NFM route. Marine and Satellite use the shared
Radio/Scope/Capture surface without dedicated signal decoders.

- **Radio** tunes and listens with the mode currently implemented for the route.
- **Scope** shows spectrum and waterfall activity.
- **Capture** records the post-demodulated audio path.

AM uses broadcast-band tuning and filtering, and WX uses NFM on a configured
NOAA weather channel. Shortwave Explorer adds AM/USB/LSB, clarifier, band-plan
AUTO and memories; the Airband dashboard implements aviation AM voice and
25/8.33 kHz tuning/scanning. See the [Shortwave changes](https://github.com/hardcoreerik/OrcSDR/blob/main/docs/shortwave/CHANGELOG.md)
and [Airband guide](https://github.com/hardcoreerik/OrcSDR/blob/main/docs/airband/README.md). Marine and Satellite still use
shared receiver routes without specialized decoders. Live reception quality
and complete on-air RF acceptance vary by band, antenna and firmware build.

## CB scanner

The CB dashboard watches all 40 channels at once from one wideband spectrum.
Press **SCAN** on the LISTEN tab: the radio stops on whichever channel someone
is talking on, plays it, waits a short hang time for a reply, then goes back to
watching the band. CH 9 is the default priority channel and interrupts other
traffic. Other tabs show the full-band spectrum, an activity log, the channel
lockout list, and scanner/audio setup. Details are in the
[CB dashboard notes](https://github.com/hardcoreerik/OrcSDR/blob/main/docs/cb/README.md).

Only one channel is heard at a time. Levels are relative, not calibrated, and
reception depends mostly on the antenna: a long outdoor wire or a proper
27 MHz antenna works far better than a short telescopic whip.
