# Feature availability and validation

This is a current-source feature guide, not a list of unfinished experiments. **Available** means the integrated feature exists in OrcSDR; **RF-validated** means recorded reception on specified hardware; **Limited / in development** identifies a real functionality boundary. A successful hardware test does not guarantee reception on every antenna, band, device revision or firmware package. The [project evidence](../../PROJECT_STATUS.md) retains exact tests and dates.

| Feature | Availability | What to expect |
|---|---|---|
| Receiver models | **Fully supported** | RTL-SDR Blog V4, V3c, V4 Lite (V4L), Nooelec NESDR SMArt V5. Nooelec XTR/E4000 is implemented in the driver but requires further OrcSDR frequency-range/dashboard integration. |
| FM broadcast, stereo, RDS, presets | **RF-validated** | Live radio and RDS tested on physical receivers. |
| AM broadcast and 119-channel scan | **Available; hardware-tested** | Receiver and scanning paths have Tab5 evidence; reception quality is antenna/band dependent and not universally calibrated. |
| NOAA Weather Radio | **Available** | Configured weather-channel NFM listening; separate current-release RF acceptance remains open. |
| CB listening and scanning | **Available** | AM/USB/LSB and 40-channel scanning; broad on-air scanner acceptance remains open. |
| Shortwave Explorer | **Available** | AM/USB/LSB, automatic band-plan modes, filters, clarifier, memories and log. General HF calibration and broad real-signal verification remain open. |
| Airband | **Available** | Dedicated 118–136.975 MHz aviation AM workspace with 25/8.33 kHz tuning, scanning and offline channel data. Physical RF acceptance remains open. |
| Marine / Satellite routes | **Limited** | General receiver routing is available; specialized decoders/dedicated workflows are not claimed. |
| P25 Phase I | **RF-validated** | Control, follow, clear-voice and encrypted-call indication/muting are documented. Encrypted audio is not decoded. |
| P25 Phase II | **In development** | Grant/sync/burst/DUID handling exists; complete payload/AMBE+2 voice is not implemented. |
| ADS-B 1090 | **RF-validated** | Live CRC-valid aircraft traffic, track comparison and FAA enrichment; live only. |
| LoRa / Meshtastic receive | **Available; hardware-tested** | Native receive, traffic, nodes, maps and RF-health views. Packet loss and coverage are not guaranteed. Receive-only. |
| POCSAG | **RF-validated at 1200 baud** | Live CAPCODE/message verification; 512/2400 baud currently have host-test evidence only; archives are memory bounded. |
| FT8 reception | **RF-validated** | 40 m FT8 decoding accepted on four supported dongles on 2026-10-09. JS8 modes are a separate development/evidence track. |
| SSTV reception | **Available; hardware-tested** | Full-colour native decoder with 14 modes in host tests and conducted Robot 36 Tab5 image reception; per-mode live RF acceptance varies. |
| ACARS VHF | **Available; RF-tested** | Native decoder/dashboard with two real off-air blocks on 2026-10-15. VDL Mode 2 is not included in that claim. |
| RF Lab / RF Visualizer | **Available** | Spectrum, IQ recording, receiver controls and test tools; not a calibrated laboratory spectrum analyzer. |
| Wi-Fi analysis | **Hardware-tested** | AP survey via ESP-Hosted; Wi-Fi changes can temporarily pause active SDR reception. |
| Global Settings | **Available; hardware-tested** | Receiver, Wi-Fi, storage, display/audio, catalog and companion settings. |
| LAN web console | **Available (trusted LAN only)** | HTTP controls and telemetry; no TLS or authentication. Do not expose directly to the Internet. |
| Android TV client | **Available companion** | Network client; Tab5 remains the radio receiver. |
| Signed data catalog | **Hardware-tested** | Signed installation and verified FAA pack workflow; other packs depend on publication. |
| SDR transmission | **Not available** | OrcSDR is receive-only; specific TX-capable devices and branches do not imply general transmit support. |

For exact release/version boundaries, hardware conditions and ongoing research, use [PROJECT_STATUS.md](../../PROJECT_STATUS.md). Historical release notes and tests remain dated evidence; the absence of a new test does not erase an implemented feature.
