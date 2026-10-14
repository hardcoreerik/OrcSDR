# OrcSDR Waveshare 4B — screen-by-screen acceptance

**Normative: do NOT shrink or globally scale Tab5 1280x720 dashboard code.**
Each screen requires an independent inventory, defined geometry/hit targets,
native compilation, physical observation and touch test before it is verified.
ESP-IDF build success does not imply screen parity.

## Home v1 (IN DEVELOPMENT, NOT DEVICE VERIFIED)

Reference: apps/orcsdr-tab5/ui/home_dashboard.cpp and home_dashboard.hpp.
Existing Tab5 Home includes browser, direct frequency keypad, mode/filter
selection, gain/AGC/bias, audio, waterfall controls, stepped VFO, and more.

| Home behavior | Waveshare Home v1 |
|---|---|
| 720x720 OrcSDR header, receiver/USB state | Implemented, awaiting device |
| Live 256-bin spectrum, not calibrated dBm | Implemented, awaiting signal |
| IQ waterfall (scrolls only on actual IQ) | Implemented, awaiting device |
| Four frequency presets; +/- frequency step | Implemented, awaiting touch |
| 960k CU8 USB receiver using pinned esp-rtl-sdr | Implemented, awaiting OTG |
| WFM/NFM/AM/SSB audio | NOT PORTED — marked IQ-only |
| Browser, keypad, gain, filter, mode, bias-T | NOT PORTED |
| SD, recording, Wi-Fi, C6, OrcDial | NOT PORTED |
| Other dashboards | NOT PORTED |

### Home gates
- [x] Tab5 action/state inventory performed
- [x] Explicit 720x720 coordinate map and 48px+ hitboxes
- [x] Host geometry, tuning clamps, known-tone FFT test
- [ ] Native ESP-IDF 5.5.4 compile (see CI)
- [ ] Identify 4B physical board and silicon revision; flash provenance
- [ ] Physical display photo: fonts, color, clipping, waterfall, RGB565
- [ ] Touch: 50 taps including each preset, +/- and step; log frequency
- [ ] No dongle: status says NO SDR CONNECTED; never display fake RF
- [ ] SDR with externally powered USB-C OTG hub pending VBUS qualification
- [ ] Enumerate; 960 kS/s stream; known signal produces expected peak
- [ ] 5-minute stream while waterfall draws, >=95% effective samples, no sustained drops
- [ ] Release gate: none until user verifies board

Next screen is not started until Home has hardware acceptance evidence.
The website's chip-family auto-detection cannot tell Tab5 from Waveshare.
Waveshare remains blocked in hardware/boards.json.

For reproducibility, record actual ESP-IDF lockfile, firmware SHA and serial
log; keep logs next to scripts run on the bench.
