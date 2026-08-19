# Getting started

## Hardware

1. Seat the Tab5 securely and connect a supported RTL-SDR Blog V4 to the USB-A host port.
2. Attach the antenna appropriate for the band you intend to receive.
3. Insert the prepared microSD card for databases, maps, captures, and screenshots.
4. Use a stable power source. A depleted external battery can remain an electrical load even when USB is attached. After flashing, unplug the PC USB Serial/JTAG cable (the COM port used by `install-tab5.ps1`). Leaving that cable connected can brownout the Tab5 under Wi-Fi + RTL load even when battery or wall power is otherwise fine.
5. Press the Tab5 power button and allow the staged USB-host and receiver startup to finish.

## First boot

The status screen reports whether the RTL-SDR is ready. Open the radio, use **NAV** to choose a band, and use the gear for global Settings. Wi-Fi and phone pairing are optional; reception does not require either.

## Installation

OrcSDR firmware releases use the repository's native ESP-IDF release artifacts and flash arguments. Do not use PlatformIO builds with the current Tab5 firmware. Follow the copy-and-paste commands published with the selected release and preserve a verified recovery image before replacing known-good firmware.
