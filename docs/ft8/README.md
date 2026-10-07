# OrcSDR FT8 RX

Status: **design and sandbox development**

Branch: `codex/ft8-rx-dashboard-sandbox`

FT8 support is receive-only for the initial OrcSDR implementation. The feature is designed as an offline-first, touch-first FT8 receiver for the M5Stack Tab5 and RTL-SDR-class hardware. It must not imply a decode, station identity, location, or protocol classification that the receiver did not actually establish.

## Product goals

- One-touch FT8 reception for users who do not already know amateur-radio operating conventions.
- Manual FT8 band selection for experienced users.
- **FT8 Hunter** that can search conventional FT8 activity frequencies and identify the best active band.
- A clear 15-second receive/decode rhythm.
- On-device decode history and station-heard views.
- Offline Maidenhead locator mapping when a decoded message actually contains a valid locator.
- No Internet dependency for core receive, scan, decode, history, or map behavior.
- RX only until a separate transmit design and safety review exists.

## Dashboard

The planned tabs are:

1. **LIVE** — current band, slot clock, waterfall/spectrum activity, decoder state, and newest decodes.
2. **DECODES** — session decode table with UTC, SNR, DT, audio offset, type, message, and grid.
3. **MAP** — offline display of station-reported Maidenhead locators.
4. **HUNTER** — FT8 band discovery and activity ranking.
5. **HEARD** — unique decoded callsigns and recent reception facts.
6. **SETUP** — decoder, clock, gain, receiver, logging, and Hunter controls.

Manual band presets remain available from HUNTER so discovery and direct tuning share one screen.

## Truth model

Hunter and the decoder must keep these observations distinct:

- **QUIET** — no useful activity above the configured detector threshold.
- **ENERGY** — RF/audio energy exists in the expected passband.
- **FT8 SIGNATURE** — synchronization/tone evidence is consistent with FT8.
- **VALID DECODE** — an FT8 frame passed decoder validity checks and produced a message.

Energy alone is never presented as FT8. A locator is never presented as a measured station location; it is a station-reported locator decoded from a message.


## Host validation

Run the FT8 model and Hunter regression suite with:

```bash
bash tools/test-ft8.sh
```

On the project's normal Windows + WSL development environment:

```powershell
.\tools\test-ft8.ps1
```

The suite builds optimized binaries and AddressSanitizer/UndefinedBehaviorSanitizer variants for the pure FT8 model and Hunter state machine. Device-level M5GFX and RF/DSP validation remain separate gates.

## Related design documents

- [FT8 dashboard vision](FT8_DASHBOARD_VISION.md)
- [FT8 RX architecture](FT8_RX_ARCHITECTURE.md)
- [FT8 implementation plan](FT8_IMPLEMENTATION_PLAN.md)
- [FT8 OrcDial design](FT8_ORCDIAL_DESIGN.md)
