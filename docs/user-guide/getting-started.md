# Getting started

**Current instructions:** [Getting Started in the OrcSDR wiki](https://github.com/hardcoreerik/OrcSDR/wiki/Getting-Started). The instructions below describe an older RC4 package and are retained as historical reference. For the published beta.1 package, use the wiki's Windows installer path to preserve saved settings; M5Burner resets them.

## Hardware

1. Seat the Tab5 securely and connect an accepted receiver. RTL-SDR Blog V4 is
   the tested baseline; see [feature status](feature-status.md) for experimental
   receiver boundaries.
2. Attach an antenna appropriate for the band you intend to receive.
3. Insert the prepared microSD card if you need databases, maps, captures, or screenshots.
4. Use stable power. After installation, disconnect the PC USB Serial/JTAG
   cable; on the tested bench it can contribute to brownout under Wi-Fi plus RTL load.

## Install with M5Burner

The current release is
[`v0.3.0-beta.1`](https://github.com/hardcoreerik/OrcSDR/releases/tag/v0.3.0-beta.1).
Open M5Burner, select the OrcSDR Tab5 package, and burn it. **An M5Burner install resets your saved
settings** (Wi-Fi profiles, location, screen rotation), so write them down first and expect to re-enter
them. The package carries matching ESP-Hosted 3.0.6 firmware for the onboard C6.

To keep your settings, use the settings-safe installer zip from the release page instead: unzip it, plug in
the Tab5 and double-click `install.bat`. It writes only the program, never your saved settings. Details are in
[`docs/M5BURNER_RELEASE.md`](https://github.com/hardcoreerik/OrcSDR/blob/main/docs/M5BURNER_RELEASE.md).

Current M5Burner search visibility was not independently reverified during the
documentation audit. If the entry is not visible, use the exact release page
above rather than an older 2.12.6 installer path.

## First boot

Boot lands on Home. Receiver and Wi-Fi startup are staged. Reception does not
require Wi-Fi; optional Wi-Fi scan/connect/catalog actions temporarily pause an
active radio session and then attempt to restore it.

## Developer build

Source builds use native ESP-IDF 5.5.4 and matching ESP-Hosted 3.0.6. Do not use
PlatformIO. Follow the [developer reference](reference/developer.md) and
[migration/acceptance record](tab5-esp-hosted-3-migration.md). A P4-only flash
does not prove or necessarily replace the C6 image, and a successful build is
not hardware or RF acceptance.
