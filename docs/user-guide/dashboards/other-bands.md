# AM, CB, Weather, and shared receiver routes

The current application has dedicated AM, CB, Airband, and Weather dashboards
plus a shared Radio/Scope/Capture receiver surface. Shortwave, Marine, and
Satellite still route into that shared surface; those catalog entries are not
separate full decoder applications.

- **Radio** tunes and listens with the mode currently implemented for the route.
- **Scope** shows spectrum and waterfall activity.
- **Capture** records the post-demodulated audio path.

AM uses broadcast-band tuning and filtering. Weather has its own five-tab
surface while continuing to reuse the existing WX/NFM receiver and audio path.
Opening Weather does not tune the receiver; NOAA Listen and Scan are explicit
user actions. The scan covers 162.400, 162.425, 162.450, 162.475, 162.500,
162.525, and 162.550 MHz sequentially with one tuner. See
[Weather dashboard](weather.md).

Shortwave, Marine, and Satellite currently use generic Browse/NFM routing, so
those routes do not establish complete shortwave AM/SSB or satellite decode.
The older standalone Browse navigation entry is retired; the shared surface
remains the implementation used by those band-entry routes.

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
