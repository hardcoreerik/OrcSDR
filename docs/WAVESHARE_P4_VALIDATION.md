# Waveshare ESP32-P4 second-board validation

**Status:** Hardware-verified (driver portability + application shell)  
**Date:** 2026-08-11  
**Branch baseline:** `codex/ads-b-dashboard`  
**Driver:** `rtl_sdr_v4_esp` **v0.4.1 — unmodified for this port**

## Why this matters

OrcSDR’s Tab5 app and the RTL-SDRv4-ESP driver were developed and measured on
**M5Stack Tab5**. Gate 4 in `PORTING.md` required repeating the RF path on a
**second ESP32-P4 board** without baking Tab5 BSP into the driver.

This report records that the **USB host driver worked out of the box** on a
Waveshare ESP32-P4-Module-DEV-KIT. Board-specific work was limited to display,
USB port selection, Ethernet web UI, and app shell — **not** transfer tables,
identity filter, or bulk pipeline.

## Device under test

| Field | Value |
|---|---|
| Board | Waveshare **ESP32-P4-Module-DEV-KIT** |
| Chip | ESP32-P4 revision **v1.3** |
| Flash / PSRAM | 16 MB flash; PSRAM present |
| MAC (measured) | `80:f1:b2:d1:e0:d5` |
| Console | Type-C CH343 **COM3**, 115200 |
| RTL-SDR | Official **Blog V4** `0bda:2838`, high-speed |
| App | `apps/orcsdr-waveshare` |

## Driver changes required

| Item | Result |
|---|---|
| Patch `rtl_sdr_v4_esp` source | **None** |
| New EP0 / bulk tables | **None** |
| New VID/PID filter | **None** |
| Tab5 `M5.Power.setExtOutput` | **Not used** (not required on this board) |
| App link | PlatformIO `lib_extra_dirs` + `lib_deps = rtl_sdr_v4_esp` |

```text
RTL_INSTALL ok v0.4.1 caps=0x0000001f
RTL_SDR_PROBE_OK v4=true vid=0bda pid=2838 hs=1 product=Blog V4
```

## Board BSP (application only)

### USB host port (hardware-verified)

The kit exposes **four Type-A ports**. Only one path enumerates the Blog V4
under the ESP USB host stack used here:

| Port | Result |
|---|---|
| **Lower-left Type-A, next to Ethernet RJ45** | **Works** |
| Other three Type-A ports | Do not use for RTL-SDR on this target |

Also set the **USB OTG jumper to HOST**.

### Display / touch (not driver)

MPI2418-style 2.4" **ILI9341** SPI panel on the 26-pin header. Pin map and
init sequence taken from the measured OrcLink `firmware/waveshare-p4` target
(GPIO SCLK=0, MOSI=3, MISO=2, DC=6, RST=20, CS=36; touch CS=32, IRQ=21).

### Ethernet web UI (not driver)

Onboard IP101 RMII (MDC=31, MDIO=52, RESET=51) serves a browser dashboard so
the small panel is not required for full ADS-B / FM chrome.

## RF measurements (observed)

### ADS-B 1090 MHz @ 2.048 MS/s

| Metric | Observed |
|---|---|
| Commanded rate | `RTL_SDR_V4_ESP_RATE_2048K` |
| Effective SPS | ~2.03–2.05 MS/s while streaming |
| IQ drops | Single-digit over multi-minute runs after start |
| LO | 1 090 000 000 Hz |
| Decoder | Tab5 `adsb_decoder` reused without fork |
| Live aircraft | CRC-valid Mode-S frames; ICAOs published to web UI |

Example serial shape:

```text
RTL_START ESP_OK mode=ADSB rate=2048000 frequency_hz=1090000000
ORC_METRICS mode=ADSB sps=204xxxx ... preambles=... crc=... mag=0..2xx
```

### FM broadcast @ 960 kS/s

| Metric | Observed |
|---|---|
| Commanded rate | `RTL_SDR_V4_ESP_RATE_960K` |
| Effective SPS | ~0.94–0.96 MS/s while streaming |
| LO example | 96.1 MHz |
| App demod | On-device WBFM → 48 kHz mono PCM ring |
| Browser | `/fm` pulls `/api/audio` PCM1 frames for Web Audio |

```text
RTL_START ESP_OK mode=FM rate=960000 frequency_hz=96100000
```

## What this does *not* claim

- Five-minute formal soak log attached as a permanent artifact (still open P1)
- Unplug/replug recovery without reboot (still open Gate 3)
- Full Tab5 1280×720 UI on the Waveshare LCD
- ES8311 speaker path on Waveshare (browser audio is the current FM listen path)
- ESP32-S2/S3 Full-Speed support (still out of measured scope)

## How to reproduce

```powershell
cd apps/orcsdr-waveshare
pio run -e waveshare_p4_adsb -t upload --upload-port COM3
pio device monitor -e waveshare_p4_adsb
```

1. OTG jumper **HOST**  
2. Blog V4 in **lower-left USB-A by Ethernet**  
3. Ethernet cable for web UI; note `ETH_STATUS ... ip=`  
4. Open `http://<ip>/` (ADS-B) or `http://<ip>/fm` (FM radio)

Full operator notes: [`apps/orcsdr-waveshare/README.md`](../apps/orcsdr-waveshare/README.md).

## Conclusion

| Question | Answer |
|---|---|
| Did the driver need Waveshare-specific changes? | **No** |
| Did Blog V4 enumerate and stream on a second P4? | **Yes** |
| Is Gate 4 (second board) closed for core RF? | **Yes — hardware-verified** |

Tab5 remains the product reference UI. Waveshare proves **RTL-SDRv4-ESP
portability** and hosts a network-first second-board shell.
