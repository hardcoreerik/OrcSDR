# OrcSDR on Waveshare ESP32-P4

Second-board multi-mode port of **`codex/ads-b-dashboard`** for the measured
**Waveshare ESP32-P4-Module-DEV-KIT** (COM3 / CH343) with the MPI2418-style
2.4-inch SPI panel on physical header pins 1–26.

> **Driver portability:** `components/rtl_sdr_v4_esp` is used **as published**
> (v0.4.1). No Waveshare-specific patches to EP0 tables, identity filter, or
> bulk pipeline. See [`docs/WAVESHARE_P4_VALIDATION.md`](../../docs/WAVESHARE_P4_VALIDATION.md).

This is **not** the full Tab5 1280×720 UI. It reuses the portable RF stack on a
compact 320×240 shell plus Ethernet web dashboards.

| Piece | Source |
|---|---|
| Display bring-up (ILI9341) | OrcLink `firmware/waveshare-p4` pin map + init |
| Touch (XPT2046) | Same SPI bus; press cycles modes |
| RTL-SDR Blog V4 USB host | `components/rtl_sdr_v4_esp` |
| Mode-S / ADS-B decoder | `apps/orcsdr-tab5/ui/adsb_decoder.*` |

## Modes

| Mode | LO / rate | UI |
|---|---|---|
| **ADSB** | 1090 MHz @ 2.048 MS/s | ICAO / callsign / altitude list |
| **FM** | default 96.1 MHz @ 960 kS/s | frequency, signal dBFS, spectrum bars |
| **WX** | default 162.400 MHz @ 960 kS/s | same radio chrome |
| **STATUS** | no stream | driver / heap / USB port hint |

Touch (firm stylus/nail) cycles modes. Serial commands also work.

## USB host port (hardware-verified)

This kit exposes **four Type-A ports**. Only one path enumerates the RTL-SDR.

| Port | Result |
|---|---|
| **Lower-left Type-A, next to the Ethernet RJ45** | **Works** |
| Other three Type-A ports | Do not use for RTL-SDR |

Set the board **USB OTG function jumper to HOST** before attaching the dongle.
Power/console stay on **Type-C CH343 (COM3)**.

Measured kit: ESP32-P4 rev 1.3, MAC `80:f1:b2:d1:e0:d5`.

## Display map

| Signal | Physical pin | ESP32-P4 GPIO |
|---|---:|---:|
| TP_IRQ | 11 | 21 |
| LCD_RESET | 13 | 20 |
| LCD_DC / LCD_RS | 15 | 6 |
| LCD_MOSI / TP_MOSI | 19 | 3 |
| TP_MISO | 21 | 2 |
| LCD_SCLK / TP_SCLK | 23 | 0 |
| LCD_CS | 24 | 36 |
| TP_CS | 26 | 32 |

Controller: ILI9341, 320×240 landscape, RGB565, MADCTL `0x28`.

## Serial CLI (115200)

```
MODE ADSB|FM|WX|STATUS
FREQ <hz>
+ / -          step FM 200 kHz or WX 25 kHz
START
STOP
HELP
```

## Build / flash

```powershell
cd F:\Ai\OrcSDR_Waveshare\apps\orcsdr-waveshare
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e waveshare_p4_adsb -t upload
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device monitor -e waveshare_p4_adsb
```

Flashing replaces OrcLink on the kit.

## Web UI (Tab5-lookalike)

The onboard 320×240 panel is a status/control surface only. The full ADS-B
dashboard is served over **Ethernet** and mirrors the Tab5 layout:

| Tab | Content |
|---|---|
| RADAR | Canvas PPI + selected aircraft summary |
| LIST | Scrollable aircraft rows |
| TARGET | Full identity / kinematics card |
| STATS | Rate, signal, SPS, drops |
| SETTINGS | Receiver lat/lon (localStorage + device), mode switch |

### Network

- PHY: onboard IP101 (MDC 31, MDIO 52, RESET 51) — same as OrcLink Waveshare
- DHCP address printed on serial: `ETH_STATUS link=true ip=…`
- Also shown on LCD **STATUS** page: `http://x.x.x.x/`

```
GET  /              Tab5-style ADS-B SPA
GET  /fm            Custom FM radio station UI (browser audio)
GET  /api/state     live JSON snapshot (+ fm spectrum / PCM stats)
GET  /api/audio     binary PCM1 frames (48 kHz s16le mono) for Web Audio
POST /api/location  {"latitude":..,"longitude":..,"radar_range_nm":25}
POST /api/mode      {"mode":"ADSB"|"FM"|"WX"}
POST /api/freq      {"frequency_hz":96100000}
```

### FM browser audio path

1. Device demodulates WBFM IQ → 48 kHz mono PCM (1 s PSRAM ring).
2. Browser polls `/api/audio` and plays via **Web Audio API**.
3. Browser DSP rack: volume, bass/mid/treble, presence, compressor, stereo width, gate.

This keeps heavy listening/EQ on the PC where effects stay clean; the P4 stays on RF + light demod.

Plug Ethernet, open the IP in a desktop browser.

## Board notes vs Tab5

- No `M5.Power.setExtOutput` — USB-A host path used as-is.
- Console is CH343 UART on COM3, not Tab5 USB Serial/JTAG.
- **Audio (ES8311 speaker) not wired yet** — FM/WX show RF level + spectrum only.
- Web UI is the primary “big screen”; LCD stays compact.

## Next gates

- [ ] ES8311 speaker path for FM/WX demod
- [ ] FM/WX web pages matching Tab5 radio chrome
- [ ] WebSocket push instead of 1 Hz poll
- [ ] Ethernet rtl_tcp optional sidecar
