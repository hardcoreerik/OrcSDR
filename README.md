# OrcSDR

![OrcSDR logo above a glowing radio spectrum and waterfall](docs/images/orcsdr-tab5.png)

**Turn an M5Stack Tab5 and a compatible RTL-SDR receiver into a portable, touchscreen software-defined radio.** OrcSDR runs its radio processing, dashboards, audio, and controls on the Tab5—no laptop, Raspberry Pi, or desktop SDR application is required after installation.

[Download the latest release](https://github.com/hardcoreerik/OrcSDR/releases/latest) · [Install with M5Burner](docs/user-guide/getting-started.md) · [Read the User Guide](docs/user-guide/index.md) · [Report a bug](https://github.com/hardcoreerik/OrcSDR/issues/new?template=bug_report.md)

## Why OrcSDR exists

OrcSDR started with a simple question: **how far can the ESP32-P4 be pushed as an actual software-defined radio host?** The first step was building a clean-room USB driver that let the microcontroller own an RTL-SDR as a native embedded peripheral. Once the Tab5 could sustain the IQ stream itself, that driver became the foundation for a complete radio.

The goal is not to hide a Linux computer behind a touchscreen or reproduce every part of a desktop SDR workstation. It is to build a useful, portable radio appliance: quick to start, touch-first, battery-powered, and able to listen, decode, visualize, scan, and explore signals without another computer attached.

That idea also shapes how OrcSDR is developed. The touchscreen and serial control interface exercise the same radio functions, making repeatable tests and automation part of the product rather than an afterthought. DSP changes are checked with saved captures and replay tools, while receiver controls and RF behavior are verified on physical hardware. Features are described as tested, experimental, or unfinished according to the evidence available.

OrcSDR is an open-source hobby project created by a radio and electronics tinkerer, with extensive AI assistance, outside review, and community testing. AI helps make ambitious work approachable, but the code still has to survive real USB hardware, real antennas, real signals, and repeatable regression tests.

Read the [RTL-SDR Blog feature](https://www.rtl-sdr.com/orcsdr-running-rtl-sdr-directly-on-an-esp32-p4/) for more of the project's story.

## Get started

1. Open **M5Burner**, search for **OrcSDR**, and burn the current release to an M5Stack Tab5.
2. Restart the Tab5, then connect a supported RTL-SDR receiver to its USB host port.
3. Connect an antenna suitable for the signals you want to receive and choose a dashboard.

Current release images include the matching ESP-Hosted C6 firmware used by the Tab5. Installation, updating, and recovery instructions are in the [Getting Started guide](docs/user-guide/getting-started.md) and [troubleshooting guide](docs/user-guide/troubleshooting.md).

## What OrcSDR does

- **Home:** tune and listen while viewing the live spectrum, waterfall, signal level, receiver status, and recently used dashboards.
- **FM Radio:** receive broadcast FM with stereo audio, RDS station information, presets, and station tuning tools.
- **AM Radio:** receive broadcast AM with region-aware channel steps, presets, automatic tuning, and a full-band station scan.
- **Shortwave:** explore HF signals with a general-purpose shortwave receiver workspace and selectable demodulation modes.
- **Weather:** quickly tune the standard NOAA weather-radio channels.
- **Airband:** listen to VHF aviation voice channels and use saved airport and frequency presets.
- **Marine:** listen across the standard VHF marine channel plan.
- **CB Radio:** tune the 40-channel Citizens Band service using AM or supported sideband modes.
- **P25 Radio:** monitor trunked P25 control channels and follow supported voice traffic.
- **ADS-B:** receive 1090 MHz aircraft broadcasts and show decoded aircraft, position, altitude, and flight details.
- **LoRa:** passively monitor LoRa and Meshtastic traffic with spectrum, packet, node, traffic, map, and RF-health views.
- **POCSAG:** receive pager traffic, identify CAPCODEs, and retain decoded messages in a local archive.
- **Satellite:** provides a dedicated workspace for receiving and inspecting satellite signals.
- **RF Lab:** make live RF measurements, inspect receiver behavior, record IQ data, and open full-screen visualization tools.
- **2.4 GHz Wi-Fi:** survey nearby access points, channels, signal strength, and advertised network security using the Tab5 wireless co-processor.
- **Settings:** manage audio, display, Wi-Fi, storage, data packs, firmware information, and device updates.

Feature behavior and current limits are maintained in the [feature status](docs/user-guide/feature-status.md) and [dashboard guides](docs/user-guide/dashboards/fm.md).

## OrcSDR on the Tab5

![M5Stack Tab5 running OrcSDR with a live FM spectrum and waterfall](docs/images/orcsdr-tab5-live.jpeg)

*OrcSDR running a live FM spectrum and waterfall on the M5Stack Tab5.*

### RF Lab dashboard

![OrcSDR RF Lab dashboard showing a live spectrum, waterfall, and receiver measurements](docs/images/orcsdr-rf-lab-dashboard.jpg)

RF Lab turns the receiver into a portable test bench with four focused tabs:

- **Live:** shows the spectrum and waterfall beside continuously updated peak frequency, peak power, noise floor, SNR, channel power, 99% occupied bandwidth, frequency error, clipping, DC offset, and I/Q balance.
- **Controls:** provides capability-aware frequency stepping, sample-rate selection, PPM correction, automatic or manual tuner gain, RTL digital AGC, and Bias-T control. Settings can either be kept or restored when leaving RF Lab.
- **Measurements:** takes individual snapshots or timed measurement runs, adds markers, compares readings with an optional reference source and path-loss value, and exposes guided measurement recipes.
- **Records:** shows whether SD storage is available, lists recent measurement sessions and results, and identifies the saved session folders that can be exported through OrcSDR's SD tools.

### FM dashboard

![OrcSDR FM dashboard tuned to 96.1 MHz and displaying live RDS station information](docs/images/orcsdr-fm-dashboard.jpg)

The FM dashboard organizes listening, station information, and diagnostics into five tabs:

- **Listen:** presents the tuned frequency, preset number, relative signal level, station name and radio text, running and stereo state, left/right audio meters, seek buttons, frequency steps, and direct frequency entry.
- **Spectrum:** shows a live spectrum and waterfall with center frequency, DSP filter bandwidth, audio/IQ activity, adjustable span, step controls, and tap-to-tune interaction.
- **Station / RDS:** expands the currently playing station and decoded RDS fields, including Program Service name, RadioText, PI code, and PTY, alongside stereo, pilot-carrier, and decoder-lock status.
- **RF Health:** reports effective versus requested sample rate, USB overruns, IQ consumer drops, audio underruns, DSP load, audio-buffer pressure, Wi-Fi state, driver state, and the most recent radio error.
- **Settings:** controls sound, volume, tuning step, filter bandwidth, spectrum graphics, and recording; it also provides preset scanning/rebuilding, device settings, and a return to Home while FM continues playing.

### ADS-B dashboard

![OrcSDR ADS-B dashboard showing its aircraft radar, receiver status, statistics, and aircraft list](docs/images/orcsdr-adsb-dashboard.jpg)

The ADS-B dashboard receives Mode-S and ADS-B broadcasts on 1090 MHz and divides aircraft tracking into five tabs:

- **Radar:** shows receiver and map status, aircraft and message totals, message rate, selected range, dropped samples, plotted aircraft positions, a compact aircraft list, signal level, positions per minute, and altitude distribution.
- **List:** provides a browsable set of received aircraft with callsign or registration, ICAO address, aircraft type and operator when known, altitude, speed, range, bearing, vertical rate, and last-seen state.
- **Target:** expands one selected aircraft with identity and registration data, altitude, speed, heading, climb or descent rate, range, bearing, latitude, longitude, last-seen status, and signal level. Installed aviation data can also provide a nearby ATC listening shortcut.
- **Stats:** charts signal strength, message rate, and Mode-S activity; summarizes aircraft, message, sample-rate, and USB/DSP-drop counters; and reports the availability of FAA aircraft data, aviation frequencies, the offline map, and ATC audio.
- **Settings:** sets the receiver location and radar range, shows the active RF-gain state, and manages FAA aircraft data, airport and frequency data, the offline map pack, and manual ATC listening.

## Visualization modes

![Six OrcSDR visualization modes: spectrum and FFT, phosphor persistence, 3D spectrum history, polar and phase, channel occupancy, and channelized tiles](docs/images/orcsdr-visualization-modes.jpg)

*A selection of OrcSDR's full-screen RF visualization modes.*

## Receiver support

| Receiver | Status | Notes |
| --- | --- | --- |
| RTL-SDR Blog V4 | Tested baseline | Full OrcSDR receiver path, including the V4 HF upconverter and supported controls. |
| RTL-SDR Blog V3 | Experimental | Provisional operation at 24 MHz and above. Hardware reports and serial logs are welcome. |
| Nooelec NESDR SMArt V5 | Experimental | Provisional operation at 24 MHz and above. Hardware reports and serial logs are welcome. |

Antenna choice affects what can be received. Useful test reports include the receiver model, frequency and band, antenna, gain setting, OrcSDR version, observed behavior, and a serial log when available.

## Technical overview

| Area | Implementation |
| --- | --- |
| Main processor | ESP32-P4 on the M5Stack Tab5; radio control, USB, DSP, graphics, touch, and audio run locally. |
| Display and controls | 1280 × 720 touchscreen interface built with M5Unified and M5GFX. |
| Receiver connection | ESP32-P4 High-Speed USB Host connected directly to a supported RTL-SDR receiver. |
| IQ transport | Callback-only IQ delivery using three 32 KiB USB transfers, with stream and consumer-drop diagnostics. |
| Signal processing | Native on-device demodulation, spectrum and waterfall generation, RF measurements, and protocol decoders. |
| Audio | 48 kHz stereo processing and playback through the Tab5 speaker path. |
| Wireless | The onboard ESP32-C6 provides Wi-Fi over SDIO through ESP-Hosted 3.0.6. Matching C6 firmware is embedded in release images. |
| Storage | Internal flash and NVS for firmware and settings, plus optional microSD storage for maps, data packs, logs, and IQ captures. |
| Build system | Native ESP-IDF 5.5.4 with locked component versions and repository build/install scripts. |

```text
Antenna → RTL-SDR tuner → High-Speed USB → esp-rtl-sdr → IQ buffers
                                                        ├─ DSP and decoders
                                                        ├─ touchscreen UI
                                                        ├─ speaker audio
                                                        └─ optional storage
```

The standalone [`esp-rtl-sdr`](https://github.com/hardcoreerik/esp-rtl-sdr) component is a clean-room USB host driver. OrcSDR keeps receiver detection and hardware capabilities in the driver while the application owns dashboards, DSP, audio, and user workflows. The current streaming configuration is intentionally fixed at three 32 KiB transfers to preserve the tested hardware baseline.

## Documentation

- [User Guide](docs/user-guide/index.md)
- [Downloads and installation](docs/user-guide/downloads.md)
- [Common controls and workflows](docs/user-guide/shared-controls.md)
- [Settings and Wi-Fi](docs/user-guide/settings.md)
- [Troubleshooting](docs/user-guide/troubleshooting.md)
- [Developer reference](docs/user-guide/reference/developer.md)
- [GitHub Wiki](https://github.com/hardcoreerik/OrcSDR/wiki)

## For developers

Source-build requirements and commands live in the [developer reference](docs/user-guide/reference/developer.md). Driver contracts, porting notes, hardware measurements, and architecture decisions live in the repository's [`docs/`](docs/) directory.

Please keep changes focused and preserve the distinction between a successful build, a successful flash, and physical radio testing. When contributing receiver behavior, document the hardware, test setup, and source of any measurements.

## Project status and support

OrcSDR is under active development. Stable behavior is protected around the RTL-SDR Blog V4 baseline, while other receiver profiles and some digital-radio features remain experimental.

- [Latest release and release notes](https://github.com/hardcoreerik/OrcSDR/releases/latest)
- [Open issues](https://github.com/hardcoreerik/OrcSDR/issues)
- [Discussions](https://github.com/hardcoreerik/OrcSDR/discussions)
- [Contributing and development documentation](docs/)

## License

OrcSDR is licensed under [GNU AGPL-3.0-only](LICENSE), with separate commercial licensing available as described in [LICENSING.md](LICENSING.md).

---

**Hardware:** [M5Stack Tab5](https://docs.m5stack.com/en/core/Tab5) · **Receiver driver:** [`esp-rtl-sdr`](https://github.com/hardcoreerik/esp-rtl-sdr) · **Guide:** [OrcSDR User Guide](docs/user-guide/index.md)
