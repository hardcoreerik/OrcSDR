# OrcSDR T-Dongle-S2 bench

Minimal ESP32-S2 / RTL-SDR Blog V4 USB-host validation bench. It is not the
Tab5 radio UI and it does not store IQ.

## Build

```powershell
. 'C:\Espressif\frameworks\esp-idf-v6.0.2\export.ps1'
Set-Location F:\Ai\OrcSDR\apps\orcsdr-tdongle-s2
idf.py build
```

Flash only after hardware-write approval:

```powershell
idf.py -p COM26 flash
```

## Bench result

With the Blog V4 already attached through OTG at boot, the app waits up to
10 seconds, streams 960 kS/s CU8 IQ for 3 seconds, discards each callback
block, stops, and saves the outcome in NVS. The screen shows the state,
completed-block count, error count, and final result.

The serial console emits the persisted record as `RTL_S2_RESULT ...` on boot.
If a console is available after reconnecting the PC, send `RTL_S2_RESULT` to
print it again. USB VBUS loss or disconnect is recorded as `POWER_UNSTABLE`;
use a powered hub before drawing any driver conclusion from that result.
