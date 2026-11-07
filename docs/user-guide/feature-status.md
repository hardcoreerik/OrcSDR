# Feature availability and validation

This is a current-source feature guide, not a list of unfinished experiments. **Available** means the integrated feature exists in OrcSDR; **RF-validated** means recorded reception on specified hardware; **Limited / in development** identifies a real functionality boundary. A successful hardware test does not guarantee reception on every antenna, band, device revision or firmware package. The [project evidence](https://github.com/hardcoreerik/OrcSDR/blob/main/PROJECT_STATUS.md) retains exact tests and dates.

| Feature | Availability | What to expect |
|---|---|---|
| Receiver models | **Fully supported: V4, V3c, V4L, Nooelec V5** | E4000/XTR is also **usable in OrcSDR within supported tuner frequencies**. Out-of-range band dashboards may open without issuing an invalid tune; capability messaging can improve. Generic/older device variants are not automatically certified. |
| FM broadcast, stereo, RDS, presets | **RF-validated** | Live radio and RDS tested on physical receivers. |
| AM broadcast and 119-channel scan | **Available; hardware-tested** | Receiver and scanning paths have Tab5 evidence; reception quality is antenna/band dependent and not universally calibrated. |
| NOAA Weather Radio | **Available** | Configured weather-channel NFM listening; separate current-release RF acceptance remains open. |
| CB listening and scanning | **Available** | AM/USB/LSB and 40-channel scanning; broad on-air scanner acceptance remains open. |
| Shortwave Explorer | **Available; live RF heard** | Maintainer confirms actual Shortwave reception/listening. AM/USB/LSB, band-plan AUTO, filters, clarifier, memories and log work; sensitivity/complete band-by-band qualification is separate. |
| Airband | **Available; live aviation voice heard** | Real arrival/departure voice heard on the Tab5; earlier [119.600 MHz field record](https://github.com/hardcoreerik/OrcSDR/blob/main/docs/airband/README.md) also documents voice compared with LiveATC. Scanner and 25/8.33 kHz tuning are implemented. |
| Marine / Satellite routes | **Limited** | General receiver routing is available; specialized decoders/dedicated workflows are not claimed. |
| P25 Phase I | **RF-validated** | Control, follow, clear-voice and encrypted-call indication/muting are documented. Encrypted audio is not decoded. |
| P25 Phase II | **In development** | Grant/sync/burst/DUID handling exists; complete payload/AMBE+2 voice is not implemented. |
| ADS-B 1090 | **RF-validated** | Live CRC-valid aircraft traffic, track comparison and FAA enrichment; live only. |
| LoRa / Meshtastic receive | **Available; live packets decoded** | Unencrypted real-world messages, telemetry, node info and GPS decoded. Encrypted traffic detected as **ENCRYPTED FRAME**, not decrypted. Passive LongFast receiving; region/profile and coverage limits remain. |
| POCSAG | **RF-validated at 1200 baud** | Live CAPCODE/message verification; 512/2400 baud currently have host-test evidence only; archives are memory bounded. |
| FT8 reception | **RF-validated** | 40 m FT8 decoding accepted on four supported dongles on 2026-10-09. This does not automatically certify FT4 or JS8 RF reception. |
| FT4 decoding | **Native implementation ready** | Compiled with the native FT8-family backend, including FT4 mode timing/sync and LDPC/CRC; live RF qualification is separate. |
| JS8 Normal decoding | **Native backend / narrower physical evidence** | C++ backend is compiled and runtime-wired; real recorded JS8 Normal signals have decoded in host tests. Full user-visible on-device reception acceptance remains open; other JS8 submodes are not fully qualified. |
| SSTV reception | **Available; 14 modes supported** | Maintainer confirms operational supported modes; all 14 covered by host tests. Conducted Robot 36, PD120 and Scottie S1 image/SD acceptance on V3c, V4, V4L and Nooelec V5 is separately documented. |
| ACARS VHF | **Available; live RF decoded** | Real-world VHF ACARS reception and archive, with later traffic records beyond the original two-block result. Airline-specific layouts vary; uninterpreted payloads remain raw. |
| VDL Mode 2 (VDL2) | **Available; live RF decoded** | Live AVLC/ACARS frames decoded on Tab5 (including a 213-frame / 135-message session). Aircraft position and climb-related fields interpreted where understood; unknown airline-specific data stays raw. |
| Dead Air / ENV III | **Available; real measurements** | RF events and Unit ENV III temperature/humidity/pressure working on Tab5 (maintainer-confirmed). [Guide](dashboards/dead-air.md) distinguishes real measurements and unavailable/stale channels from interpretation; no paranormal detection claim. |
| VEIL-26 | **Available; symbolic analysis** | Optional reproducible mapping of measured frequency/level to esoteric symbols and numerology. Interpretations are separated from measured physical values. No claim of messages, spirits or physical detection from the symbolic output. |
| RF Lab / RF Visualizer | **Available** | Spectrum, IQ recording, receiver controls and test tools; not a calibrated laboratory spectrum analyzer. |
| Wi-Fi analysis | **Hardware-tested** | AP survey via ESP-Hosted; Wi-Fi changes can temporarily pause active SDR reception. |
| Global Settings | **Available; hardware-tested** | Receiver, Wi-Fi, storage, display/audio, catalog and companion settings. |
| LAN web console | **Available (trusted LAN only)** | HTTP controls and telemetry; no TLS or authentication. Do not expose directly to the Internet. |
| Android TV client | **Available companion** | Network client; Tab5 remains the radio receiver. |
| Signed data catalog | **Hardware-tested** | Signed installation and verified FAA pack workflow; other packs depend on publication. |
| SDR transmission | **Not available** | OrcSDR is receive-only; specific TX-capable devices and branches do not imply general transmit support. |

Firsthand acceptance and the distinction from dated formal RF tests are recorded in the
[maintainer field note](https://github.com/hardcoreerik/OrcSDR/blob/main/docs/MAINTAINER_FIELD_ACCEPTANCE_2026-11-04.md).

For the native protocol-by-protocol source and evidence inventory, see [Native decoders](https://github.com/hardcoreerik/OrcSDR/blob/main/docs/NATIVE_DECODER_INVENTORY.md).

For exact release/version boundaries, hardware conditions and ongoing research, use [PROJECT_STATUS.md](https://github.com/hardcoreerik/OrcSDR/blob/main/PROJECT_STATUS.md). Historical release notes and tests remain dated evidence; the absence of a new test does not erase an implemented feature.
