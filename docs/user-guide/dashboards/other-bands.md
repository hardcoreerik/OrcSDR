# AM, CB, and shared receiver routes

The current application has dedicated AM, CB, Airband, and Weather dashboards
plus a shared Radio/Scope/Capture receiver surface. The dashboard-catalog entries
for Shortwave, Marine, and Satellite still use shared or generic receiver routes;
their labels do not imply complete dedicated decoder applications.

Weather is documented separately in [Weather dashboard](weather.md). Opening it
does not retune the receiver. Its NOAA **LISTEN** and **SCAN 7 CH** controls are
explicit RF actions; the scan samples the seven U.S. NOAA Weather Radio channels
sequentially and stops instead of becoming a hidden continuous listener.

- **Radio** tunes and listens with the mode currently implemented for the route.
- **Scope** shows spectrum and waterfall activity.
- **Capture** records the post-demodulated audio path.

AM uses broadcast-band tuning and filtering. Weather reuses the existing WX/NFM
receive path only when the user chooses Listen or Scan. Shortwave, Marine, and
Satellite still use generic/experimental routing, so those entries do not
establish complete shortwave or satellite decoder support. The older standalone Browse navigation entry is retired; the shared
surface remains the implementation used by these band-entry routes.

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
