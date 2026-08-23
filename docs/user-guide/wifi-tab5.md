# Tab5 Wi-Fi: proven ESP-Hosted SDIO implementation

This is the reusable Wi-Fi implementation guide for an M5Stack Tab5. It is
based on the OrcSDR P4/C6 pairing that joins a WPA network and downloads,
validates, and installs signed data packs over HTTPS. It is not an Arduino or
PlatformIO recipe.

## Known-good version set

| Part | Required value |
| --- | --- |
| P4 app | native ESP-IDF application, ESP-IDF 5.5.4 |
| ESP-Hosted host dependency | 3.0.6 |
| C6 ESP-Hosted firmware | 3.0.6, matching the host |
| Wi-Fi transport | SDIO Slot 1, 4-bit |
| Operational SDIO clock | 10 MHz |
| Display/board library | M5Unified 0.2.20 and M5GFX 0.2.27 as IDF components |

The Tab5 wiring can support a higher SDIO clock, but this configuration uses
10 MHz because it is the qualified clock for sustained HTTPS downloads on this
P4/C6 pair. Raise it only after repeating long transfer and reboot tests.

## Fixed Tab5 wiring

The C6 is an SDIO peripheral on **Slot 1**, not the removable card. The
removable microSD is on **Slot 0**. Never initialize the two with the same
slot or pins.

| Device | SDMMC slot | CLK | CMD | D0 | D1 | D2 | D3 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| On-board ESP32-C6 | 1 | 12 | 13 | 11 | 10 | 9 | 8 |
| Removable microSD | 0 | 43 | 44 | 39 | 40 | 41 | 42 |

The C6 reset line is GPIO 15, active high. Its power rail is enabled by the
Tab5 board startup (`M5.begin()`); do not blindly power-cycle it on every P4
boot. The Wi-Fi antenna selector is I/O expander `0x43`, pin 0: low selects the
internal antenna and high selects external MMCX.

## Required project configuration

Keep these in `sdkconfig.defaults`, not in a generated `sdkconfig` cache:

```ini
CONFIG_ESP32P4_TAB5_C6_BOARD=y
CONFIG_ESP_HOSTED_HOST_SDIO_CLK_KHZ=10000
CONFIG_ESP_HOSTED_HOST_SDIO_BUS_WIDTH_4=y
CONFIG_ESP_HOSTED_SDIO_RESET_ACTIVE_HIGH=y
CONFIG_ESP_HOSTED_HOST_RESET_GPIO=15
CONFIG_ESP_HOSTED_HOST_SDIO_PIN_CLK=12
CONFIG_ESP_HOSTED_HOST_SDIO_PIN_CMD=13
CONFIG_ESP_HOSTED_HOST_SDIO_PIN_D0=11
CONFIG_ESP_HOSTED_HOST_SDIO_PIN_D1=10
CONFIG_ESP_HOSTED_HOST_SDIO_PIN_D2=9
CONFIG_ESP_HOSTED_HOST_SDIO_PIN_D3=8
CONFIG_ESP_HOSTED_AUTO_CALL_INIT_BEFORE_APP_MAIN=n
CONFIG_ESP_HOSTED_HOST_CP_RESET_STRATEGY_ONLY_IF_NECESSARY=y
CONFIG_ESP_HOSTED_HOST_TRANSPORT_RESTART_ON_FAILURE=n
CONFIG_EH_HOST_PORT_DMA_PREFER_SPIRAM=y
CONFIG_USB_HOST_DWC_DMA_CAP_MEMORY_IN_PSRAM=y
CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y
CONFIG_MDNS_TASK_CREATE_FROM_SPIRAM=y
CONFIG_MDNS_MEMORY_ALLOC_SPIRAM=y
```

Use bounded Hosted queues (`TX=4`, `RX=2`) unless a measured workload requires
more. Keep SDIO/I2S DMA-capable allocations in internal memory and ordinary
TLS, mDNS, and large application allocations in PSRAM.

## Startup order

1. Start M5Unified/Tab5 board support. This enables the C6 power rail.
2. Explicitly start ESP-Hosted on Slot 1. Do not enable Hosted auto-init before
   `app_main`.
3. Probe the already-powered C6 first. Reset only if SDIO enumeration fails.
4. Load settings and mount the removable card independently on Slot 0.
5. Start the Wi-Fi station only after the Hosted handshake succeeds.
6. Start USB RTL-SDR/audio after the network action is finished, or pause it
   before a scan, join, or large catalog transfer and resume it afterwards.

This order prevents boot-time SDIO contention and avoids repeatedly resetting
an already working C6.

## User-facing behavior

Connectivity has three separate concepts:

- **Wi-Fi power:** enables or disables the C6 station service now.
- **Connect on boot:** off by default. When enabled, boot joins saved profile
  priority 1. When off, Wi-Fi stays available for a manual Scan or Use action.
- **Saved networks:** up to four profiles, stored on-device and ordered by
  priority. Do not expose their passwords in logs or CLI output.

The boot setting controls automatic network association, not the permanent
ESP-Hosted transport configuration. A user can still connect manually when it
is off.

For feature parity, the serial UI command is:

```text
RTL_UI ACTION SETTINGS WIFI_BOOT 1   # enable Connect on boot
RTL_UI ACTION SETTINGS WIFI_BOOT 0   # disable it
```

Normal serial observation must not reset the device:

```powershell
idf.py -B build-native-hosted3 -p COM17 monitor --no-reset
```

## Build, flash, and acceptance

```powershell
Set-Location F:\Ai\OrcSDR\apps\orcsdr-tab5
.\tools\build-tab5-idf.ps1
. C:\Espressif\frameworks\esp-idf-v5.5.4\export.ps1
idf.py -B build-native-hosted3 -p COM17 app-flash
```

An app-only flash must end with `Hash of data verified`. It does not update the
C6 image, bootloader, partition table, NVS, or microSD contents.

Require all of these before calling a Tab5 Wi-Fi implementation working:

1. Boot log reports matching host and coprocessor versions:
   `esp-hosted fw versions: host=3.0.6 coprocessor=3.0.6 (match)`.
2. The device joins a saved WPA profile and has an IP address.
3. A manual scan returns networks without a reboot or panic.
4. A signed HTTPS catalog check and at least one install complete with hash and
   schema validation, leaving the old pack active if the transfer fails.
5. Radio receive starts before and after Wi-Fi/catalog work.
6. A restart with **Connect on boot off** reaches radio readiness without an
   automatic association; enabling it joins priority 1 instead.

## Do not repeat these mistakes

- Do not use PlatformIO, Arduino IDE, or an unrelated Tab5 pin preset.
- Do not use Slot 0 for the C6 or Slot 1 for the removable card.
- Do not edit generated `sdkconfig`; rebuild from `sdkconfig.defaults`.
- Do not use a P4 app flash to claim the C6 was updated. Update the C6 with a
  matching, deliberate ESP-Hosted C6 procedure.
- Do not restart the P4 forever after an SDIO error; surface Wi-Fi unavailable
  and preserve radio recovery.
- Do not run a serial monitor, transfer tool, and flash tool against COM17 at
  the same time.

## Primary references

- [ESP-Hosted SDIO guide](https://github.com/espressif/esp-hosted-mcu/blob/main/docs/sdio.md)
- [ESP-Hosted Tab5 board profile](https://github.com/espressif/esp-hosted-mcu/blob/main/esp_hosted_fg/esp/esp_driver/network_adapter/main/esp_hosted_config.h)
- [M5Stack Tab5 native board implementation](https://github.com/m5stack/M5Tab5-UserDemo/blob/main/platforms/tab5/components/m5stack_tab5/m5stack_tab5.c)
- [OrcSDR migration record](tab5-esp-hosted-3-migration.md)
