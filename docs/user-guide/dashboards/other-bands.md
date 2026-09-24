# AM, WX, CB, and shared receiver routes

The current application has dedicated AM and CB dashboards plus a shared
Radio/Scope/Capture receiver surface. Weather and the dashboard-catalog entries
for Shortwave, Airband, Marine, and Satellite route into that shared surface;
they are not separate full decoder applications.

- **Radio** tunes and listens with the mode currently implemented for the route.
- **Scope** shows spectrum and waterfall activity.
- **Capture** records the post-demodulated audio path.

AM uses broadcast-band tuning and filtering. WX uses NFM on a configured NOAA
weather channel. Shortwave, Airband,
Marine, and Satellite currently use generic Browse/NFM routing, so this UI does
not establish correct shortwave AM/SSB, aviation AM voice, or a satellite
decoder. The older standalone Browse navigation entry is retired; the shared
surface remains the implementation used by these band-entry routes.

## CB scanner

The CB dashboard watches all 40 channels at once from one wideband spectrum.
Press **SCAN** on the LISTEN tab: the radio stops on whichever channel someone
is talking on, plays it, waits a short hang time for a reply, then goes back to
watching the band. CH 9 is the default priority channel and interrupts other
traffic. Other tabs show the full-band spectrum, an activity log, the channel
lockout list, and scanner/audio setup. Details are in the
[CB dashboard notes](../../cb/README.md).

Only one channel is heard at a time. Levels are relative, not calibrated, and
reception depends mostly on the antenna: a long outdoor wire or a proper
27 MHz antenna works far better than a short telescopic whip.
