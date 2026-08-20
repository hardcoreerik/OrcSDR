# Tab5 native ESP-Hosted 3.0.6 migration

This is the source of truth for the M5Stack Tab5 P4/C6 migration. It replaces
the old 2.12.6 pairing guidance for current development. Historical release
notes remain historical records, not current installation instructions.

## Version set

| Part | Current version / configuration |
| --- | --- |
| P4 application target | ESP32-P4, native ESP-IDF application |
| ESP-IDF used for the verified build | 5.5.4 |
| ESP-Hosted host dependency | 3.0.6 |
| C6 ESP-Hosted firmware | 3.0.6 installed through the temporary P4 bridge |
| M5Unified | 0.2.20 |
| M5GFX | 0.2.27 |
| Wi-Fi transport | SDIO, Slot 1, 4-bit, 40 MHz |
| microSD transport | SDMMC Slot 0, 4-bit |

M5Unified and M5GFX are ESP-IDF components here. They do not make OrcSDR an
Arduino or PlatformIO project.

## Tab5 buses and pins

The C6 and removable microSD use different controllers. Do not substitute one
pin map for the other.

| Function | Controller / slot | Pins |
| --- | --- | --- |
| C6 ESP-Hosted SDIO | SDMMC Slot 1, 4-bit, 40 MHz | CLK 12, CMD 13, D0 11, D1 10, D2 9, D3 8 |
| C6 reset | ESP-Hosted board profile | GPIO 15, active high |
| removable microSD | SDMMC Slot 0, 4-bit | CLK 43, CMD 44, D0 39, D1 40, D2 41, D3 42 |
| Wi-Fi antenna selector | Tab5 I/O expander 0 (`0x43`) | pin 0: low internal, high external MMCX |
| C6 power enable | Tab5 I/O expander 1 (`0x44`) | pin 0, `WLAN_PWR_EN`; initialized by `M5.begin()` |

`CONFIG_ESP32P4_TAB5_C6_BOARD=y` is required. It is the ESP-Hosted 3.x
Tab5 board profile; the explicit pins above make a wrong override obvious.

## Build and P4 flash

Use the installed ESP-IDF 5.5.4 environment. The native wrapper creates an
isolated `build-native-hosted3/sdkconfig` from `sdkconfig.defaults`, avoiding
the legacy project-level generated cache and its old 2.12.6 patch logic.

```powershell
Set-Location F:\Ai\OrcSDR\apps\orcsdr-tab5
.\tools\build-tab5-idf.ps1
```

For an authorized Tab5 bench flash on `COM17`, preserve the device backup and
write the application partition only:

```powershell
& 'C:\Espressif\python_env\idf5.5_py3.14_env\Scripts\python.exe' -m esptool `
  --chip esp32p4 --port COM17 --baud 460800 --before default_reset --after hard_reset `
  write_flash 0x10000 build-native-hosted3\orcsdr_tab5.bin
```

This does not write the bootloader, partition table, NVS, or C6. Require
`Hash of data verified` before treating the flash as successful.

## Acceptance status

The native 5.5.4 / ESP-Hosted 3.0.6 P4 application builds and has been
written application-only to the Tab5. The radio path boots, mounts microSD,
opens the RTL-SDR Blog V4, and starts FM audio.

The P4-to-C6 SDIO link is accepted on the Tab5. Verified boot serial contains:

```text
I (1889) eh_init_evt: esp-hosted fw versions: host=3.0.6 coprocessor=3.0.6 (match)
RTL_WIFI_BOOT_STATUS station=1 hosted_match=1 stage=none error=0x0
```

This proves C6 enumeration, the 4-bit 40 MHz SDIO transport, exact version
pairing, and P4 Wi-Fi station initialization. A live AP scan and visual screen
acceptance remain separate checks.

Hosted auto-init is deliberately disabled
(`CONFIG_ESP_HOSTED_AUTO_CALL_INIT_BEFORE_APP_MAIN=n`). A checked run with it
enabled produced recurring SDIO card-init failures after boot. OrcSDR instead
initializes M5Unified first (which enables the Tab5 C6 power rail), then starts
Hosted manually. `sdkconfig.defaults` is the source configuration; generated
`sdkconfig` is a cache and must not be edited as a fix.

The host uses the SDIO-specific **reset only if enumeration fails** strategy.
This avoids resetting an already powered C6 on every P4 boot while retaining
the upstream recovery path for an actual SDIO enumeration failure.

## Avoidable blockers

- Do not use PlatformIO, Arduino IDE, or the old 2.12.6 lifecycle patch.
- Do not flash `COM24`: it is the Heltec LoRa V4 bench device. The Tab5 is
  `COM17` on this bench.
- Do not let a serial monitor or transfer tool hold `COM17` during a flash.
- Do not use `idf.py flash` casually during migration: it can write more than
  the P4 app partition. Use the explicit app-only command unless a full image
  write is intentionally authorized.
- Build proof, flash proof, C6 handshake proof, Wi-Fi scan proof, and visual
  acceptance are separate evidence.
