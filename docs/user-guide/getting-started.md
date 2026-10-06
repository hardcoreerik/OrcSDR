# Getting started

## Hardware

1. Seat the Tab5 securely and connect an accepted receiver. RTL-SDR Blog V4 is
   the tested baseline; see [feature status](feature-status.md) for experimental
   receiver boundaries.
2. Attach an antenna appropriate for the band you intend to receive.
3. Insert the prepared microSD card if you need databases, maps, captures, or screenshots.
4. Use stable power. After installation, disconnect the PC USB Serial/JTAG
   cable; on the tested bench it can contribute to brownout under Wi-Fi plus RTL load.

## Install in your browser

1. Open the [OrcSDR M5Burner web flasher](https://burner.m5stack.com/share/firmware/JXFU4H)
   in **Chrome or Edge** on your computer. The page uses WebSerial.
2. Connect the **M5Stack Tab5** by USB. Close any serial monitor using its port.
3. Check the version shown on the page, click **Burn to device**, and select the
   Tab5 serial port when the browser asks for access.
4. Follow the flasher instructions and keep the USB cable connected until flashing
   completes. Restart the Tab5 afterward.

**An M5Burner install resets saved settings** (Wi-Fi profiles, location, screen
rotation), so write them down first and expect to re-enter them.

## Other installation methods

You can also use the **M5Burner desktop application**: search for OrcSDR, select
the Tab5 package, and burn it. The same settings-reset notice applies.

To keep your settings, use the Windows settings-preserving installer zip from
[the latest release page](https://github.com/hardcoreerik/OrcSDR/releases/latest):
unzip it, plug in the Tab5, and double-click `install.bat`. It writes the bootloader,
partition table, and OrcSDR application while leaving saved settings and the
existing C6 firmware untouched. Details are in
[`docs/M5BURNER_RELEASE.md`](https://github.com/hardcoreerik/OrcSDR/blob/main/docs/M5BURNER_RELEASE.md).

For supported ordinary Wi-Fi operation, C6 2.12.6 and 3.0.6 are both accepted;
their version numbers do not need to match the host. The included C6 3.0.6
update is optional. Keep C6 2.12.6 if you need M5Launcher Wi-Fi/OTA; applying the
3.0.6 update stops those M5Launcher functions.

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
