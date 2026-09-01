# OrcSDR

<p align="center">
  <strong>A handheld software-defined radio that does not need a PC.</strong><br>
  RTL-SDR Blog V4 → USB High-Speed → ESP32-P4 → touch screen, spectrum, and audio
</p>

OrcSDR turns an **M5Stack Tab5** and an **RTL-SDR Blog V4** into a radio you can pick up and use. Plug in the receiver, connect an antenna, and the Tab5 handles the display, tuning, and audio itself. No laptop, Raspberry Pi, or desktop SDR program is required.

<p align="center">
  <img src="docs/images/OrcSDR-Main.png"
       alt="OrcSDR running on an M5Stack Tab5 with an RTL-SDR Blog V4 receiver"
       width="100%">
</p>

From the splash screen, tap **OrcSDR** to open Home. It remembers where you were, shows the live spectrum and waterfall, and takes you to a focused dashboard when you want to listen to FM, watch aircraft, monitor LoRa traffic, or adjust the radio.

<p align="center">
  <a href="#get-orcsdr-running"><strong>Get OrcSDR running</strong></a>
  &nbsp;•&nbsp;
  <a href="#what-you-can-do-today"><strong>What it does</strong></a>
  &nbsp;•&nbsp;
  <a href="https://github.com/hardcoreerik/OrcSDR/wiki"><strong>Wiki</strong></a>
  &nbsp;•&nbsp;
  <a href="https://github.com/hardcoreerik/OrcSDR/issues/new"><strong>Share feedback</strong></a>
</p>

> [!NOTE]
> OrcSDR is under active development. The Tab5 is the reference device and uses native **ESP-IDF 5.5.4**. PlatformIO and the Arduino IDE are not supported build or flash paths.

## What you can do today

<p align="center">
  <img src="docs/images/dashboards/home.png"
       alt="OrcSDR Home dashboard with last-used radios, live spectrum, waterfall, and controls"
       width="100%">
</p>

| Dashboard | What to expect |
| --- | --- |
| **FM Radio** | Listen to broadcast FM, see the spectrum, and read RDS station information. |
| **ADS-B Aircraft** | Experimental 1090 MHz aircraft view. Reception depends on your antenna and location. |
| **LoRa Monitor** | Experimental, receive-only monitoring of compatible LoRa and Meshtastic traffic. |
| **P25** | Experimental clear-voice trunking work. Encrypted voice is not decoded. |
| **Settings** | Device, display, audio, storage, and optional connectivity settings. |

The [GitHub Wiki](https://github.com/hardcoreerik/OrcSDR/wiki) has the full dashboard walkthroughs, first-listen guide, and troubleshooting help. The [feature-status guide](docs/user-guide/feature-status.md) is the detailed source for what is implemented, experimental, deferred, or unavailable.

## Get OrcSDR running

### What you need

- M5Stack Tab5
- RTL-SDR Blog V4 and an antenna for the band you want to receive
- A USB cable for flashing the Tab5 from a Windows PC
- ESP-IDF 5.5.4 for Windows

### Install and flash

1. Install ESP-IDF 5.5.4, clone this repository, and keep the RTL-SDR disconnected while you flash.
2. Follow the current [Tab5 ESP-Hosted 3.0.6 migration guide](docs/user-guide/tab5-esp-hosted-3-migration.md). It covers the matching Tab5 C6 firmware, the native build, and the app-only flash command.
3. Connect the RTL-SDR Blog V4 to the Tab5 USB Host port, attach an antenna, power on, wait for **Ready**, then tap **OrcSDR**.
4. Open FM Radio and tune a familiar local station. Hearing it and seeing its signal on the spectrum is the quickest first check.

The C6 Wi-Fi firmware must match the P4 application’s ESP-Hosted **3.0.6** version. A normal P4 flash does not update the C6. The older `install-orcsdr.ps1` script remains for historical 2.12.6 releases; do not use it as the current 3.0.6 acceptance path.

For the complete first-run checklist, including antenna and power notes, see the Wiki’s [Getting Started](https://github.com/hardcoreerik/OrcSDR/wiki/Getting-Started) page and the [Tab5 Wi-Fi guide](docs/user-guide/wifi-tab5.md).

## What we are building next

We are continuing to harden the standalone radio while exploring **limited Wi-Fi analysis at 2.4 GHz**. We are also investigating Wi-Fi CSI with additional ESP32 nodes to learn what is practical for mesh experiments.

That work is early exploration, not a current downloadable receiver feature. We will share useful results as the hardware and radio path mature.

## Suggestions and real-world reports

Have an idea, a hardware report, or a result from your area? Please [open an issue](https://github.com/hardcoreerik/OrcSDR/issues/new). Tell us which Tab5 and SDR you used, the antenna and band, what you expected, and what happened. Good reports—especially from real antennas and RF environments—help shape the next release.

## RTL-SDR Blog V4 driver

OrcSDR uses the standalone [esp_rtl_sdr](https://github.com/hardcoreerik/esp-rtl-sdr) ESP-IDF USB Host driver for the RTL-SDR Blog V4. The current measured reference platform is the ESP32-P4 with USB High-Speed. See the [integration notes](docs/API_ESP_RTL_SDR.md) and [porting guide](docs/PORTING.md) for driver details.

## The Orc ecosystem

OrcSDR is part of a group of local-first projects:

- [TheOrc](https://github.com/hardcoreerik/TheOrc) — local AI orchestration and distributed compute.
- [OrcSDR](https://github.com/hardcoreerik/OrcSDR) — embedded SDR and RTL-SDR Blog V4 tooling.
- [OrcMesh](https://github.com/hardcoreerik/OrcMesh) — LoRa, Meshtastic, MeshCore, and embedded mesh networking.

## Contributing

Contributions are welcome in USB Host, DSP, radio UI, hardware testing, driver behavior documentation, test tooling, and docs. For the RTL-SDR Blog V4 driver, preserve the clean-room rules and document the source of any measured behavior.

## License

OrcSDR is licensed under **AGPL-3.0-only** by default. See [LICENSE](LICENSE) and [LICENSING.md](LICENSING.md). Commercial licensing terms are available from the maintainer.
