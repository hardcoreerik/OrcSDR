# Getting started

## Hardware

1. Seat the Tab5 securely and connect a supported RTL-SDR Blog V4 to the USB-A host port.
2. Attach the antenna appropriate for the band you intend to receive.
3. Insert the prepared microSD card for databases, maps, captures, and screenshots.
4. Use a stable power source. A depleted external battery can remain an electrical load even when USB is attached. After flashing, unplug the PC USB Serial/JTAG cable (the COM port used by `install-orcsdr.ps1`). Leaving that cable connected can brownout the Tab5 under Wi-Fi + RTL load even when battery or wall power is otherwise fine.
5. Press the Tab5 power button and allow the staged USB-host and receiver startup to finish.

## First boot

Boot lands on Home. If Auto-start reception is on, the last FM station can run in the background. Wi-Fi and phone pairing are optional; reception does not require either.

## Installation

Current development uses ESP-IDF 5.5.4 and ESP-Hosted 3.0.6. The native P4
application/radio path is working, but the permanent P4-to-C6 Wi-Fi handshake
is still under acceptance. Follow the exact build, flash, and status guidance
in [`../TAB5_ESP_HOSTED_3_MIGRATION.md`](../TAB5_ESP_HOSTED_3_MIGRATION.md).

Do not use the legacy 2.12.6 installer flow as a 3.0.6 verification step.
For a native build:

```powershell
$env:IDF_PYTHON_ENV_PATH = 'C:\Espressif\python_env\idf5.5_py3.14_env'
. 'C:\Espressif\frameworks\esp-idf-v5.5.4\export.ps1'
Set-Location .\apps\orcsdr-tab5
idf.py reconfigure
idf.py build
```

The Tab5 C6 must run matching ESP-Hosted **3.0.6**. A normal P4 application
flash does not update the C6. Do not claim Wi-Fi accepted until serial prints
the three `I OrcSDR` C6/version/transport lines in the migration document.

Do not use PlatformIO for Tab5 firmware. Preserve a recovery image before replacing known-good firmware.
