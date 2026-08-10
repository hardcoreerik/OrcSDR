# OrcSDR Tab5 app (portable RF shell + radio)

Measured M5Stack **Tab5** (ESP32-P4) + official RTL-SDR Blog V4 USB host path.

Direction: not only an FM radio — a **portable RF tool shell** where listen,
scope, and capture are first tabs, and later tools (band scan, IQ dump, gain
lab, analyzer) plug into the same shell without forking the USB/demod core.

| Tool tab | Role (now) |
|---|---|
| **RADIO** | FM / AM / WX plus 24–1766 MHz BROWSE, tune, SIG, volume |
| **SCOPE** | 256-bin Welch spectrum + peak-hold (GFX path; audio-first gating) |
| **CAPTURE** | Post-demod 48 kHz mono PCM → PSRAM → optional SD WAV (`/orcsdr/…`) |

`BROWSE` exposes the driver's full tuner range with direct entry, 1 MHz steps,
pinch/pan, and a NAV → `US BAND GUIDE` quick-jump menu. The header identifies
common CB, amateur, airband, NOAA satellite/weather, marine, FRS/GMRS,
LoRa/ISM, ADS-B, GNSS, and L-band satcom ranges as tuning moves. It is a receive
guide, not permission to transmit; allocations overlap and vary outside the US.
Reference: [NTIA US allocation chart](https://www.ntia.gov/page/united-states-frequency-allocation-chart),
[NOAA Weather Radio frequencies](https://www.weather.gov/marine/wxradio).

`BROWSE` currently uses NFM for listening. Correct AM/SSB/digital demodulation
is a separate mode/decoder gate. Coverage below 24 MHz also remains on the
experimental direct-sampling path and is not claimed by the wide-range browser.

Serial: `RTL_TOOL RADIO|SCOPE|CAPTURE`, `RTL_REC_START|STOP|STATUS|SAVE`.

### Boot / loading splash

Splash is the **loading screen** while Wi-Fi, RTL-SDR host, and NVS come up:

| Item | Detail |
|---|---|
| Animated | Variant 4 (`D_tviz`), packed as `OrcSDR_Splash_1280x720_60fps_10s.orsplash` on microSD (`/` or `/orcsdr/`) |
| Decode | ESP32-P4 HW JPEG → RGB565, SD read-ahead + double RGB buffers |
| Status line | “Loading… / Starting Wi-Fi… / Starting RTL-SDR…” during boot |
| Start control | **OrcSDR** button appears only when boot reports **ready**; animation keeps looping until tapped |
| Fallback | Poster JPEG or text if SD/asset/decode fails (same ready/button rules) |
| Pack selected asset | `python tools/splash_pack.py --frames-dir docs/splash/variants/D_tviz/frames --out <asset> --fps 24 --frame-count 240 --quality 35 --tab5-native` |
| Docs | [`docs/OrcSDR_Splash_README.md`](../../docs/OrcSDR_Splash_README.md) |

Driver component (portable USB/stream):

**[`components/rtl_sdr_v4_esp`](../../components/rtl_sdr_v4_esp)** (RTL-SDRv4-ESP).

Hard rules: clean-room V4 only; no librtlsdr; do not claim calibrated OTA RF
from UI features alone.

## Build (PlatformIO)

```powershell
# Python 3.10–3.13 required for pioarduino 55.03.38-1
cd apps/orcsdr-tab5
pio run -e m5tab5_ui
pio run -e m5tab5_ui -t upload --upload-port COMxx
```

Pins: pioarduino `55.03.38-1`, Arduino `3.3.8`, M5Unified `0.2.15`, M5GFX `0.2.21`.

`m5tab5_ui` is the complete firmware: regular splash/home flow, NAV band
selection, and the LoRa dashboard/PSRAM decoder capture path. The optional
`m5tab5_lora_test` environment uses the same radio features but skips directly
to LoRa for bench testing.

Do not override the platform ESP-P4 toolchain. After flash, power-cycle if the
device stays in download mode.

## Copy files to the installed microSD over USB

The PC-facing USB Serial/JTAG connection remains available for flashing and
also supports verified file transfer into `/orcsdr/`. A transfer started during
the splash cleanly stops the animation first. Stop radio/recording, then run:

```powershell
Set-Location F:\Ai\OrcSDR
.\tools\copy_to_tab5_sd.ps1 -Path 'F:\path\asset.orsplash' -Port COM17
```

The optional second argument selects the card path:

```powershell
.\tools\copy_to_tab5_sd.ps1 '.\asset.orsplash' `
  '/orcsdr/OrcSDR_Splash_1280x720_60fps_10s.orsplash' -Port COM17
```

Transfers use 16 KiB binary chunks, stage to `.part`, verify SHA-256 on the
Tab5, then replace the destination with rollback protection. They are limited
to 64 MiB and `/orcsdr/`; active radio or audio recording rejects a transfer.
This is not a Windows drive letter—the fixed USB Serial/JTAG interface remains
the flashing/console connection.

The firmware also accepts `SD_REMOVE <ASCII-path-as-hex>` for files below
`/orcsdr/` and the one legacy root splash filename. Removal is refused while
radio or recording is active.

## ESP-IDF serial agent

The `main/` tree is the lighter ESP-IDF serial/recovery agent (hello, snapshot,
heartbeat). Prefer PlatformIO `m5tab5_ui` for the full radio.

## Relationship

| Piece | Location |
|---|---|
| Driver (portable) | `components/rtl_sdr_v4_esp` |
| This app | `apps/orcsdr-tab5` |
| OrcLink | Separate repo — control plane, not required to run this app |
