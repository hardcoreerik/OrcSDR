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

From a clone of this repository on Windows, with ESP-IDF 5.5.3 installed:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\install-orcsdr.ps1
```

That installs `requirements.txt`, builds and flashes the P4, then reads `RTL_WIFI_HOSTED`. The Tab5 C6 must already be on ESP-Hosted **2.12.6** (M5Burner). The P4 image does not update the C6. If the pair does not match, follow the printed M5Burner steps and re-run `.\install-orcsdr.ps1 -CheckHostedOnly`.

Do not use PlatformIO for Tab5 firmware. Preserve a recovery image before replacing known-good firmware.
