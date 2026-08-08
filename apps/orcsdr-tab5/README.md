# OrcSDR Tab5 app (portable RF shell + radio)

Measured M5Stack **Tab5** (ESP32-P4) + official RTL-SDR Blog V4 USB host path.

Direction: not only an FM radio — a **portable RF tool shell** where listen,
scope, and capture are first tabs, and later tools (band scan, IQ dump, gain
lab, analyzer) plug into the same shell without forking the USB/demod core.

| Tool tab | Role (now) |
|---|---|
| **RADIO** | FM / AM / WX listen, tune, SIG, volume |
| **SCOPE** | 256-bin Welch spectrum + peak-hold (GFX path; audio-first gating) |
| **CAPTURE** | Post-demod 48 kHz mono PCM → PSRAM → optional SD WAV (`/orcsdr/…`) |

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

Do not override the platform ESP-P4 toolchain. After flash, power-cycle if the
device stays in download mode.

## ESP-IDF serial agent

The `main/` tree is the lighter ESP-IDF serial/recovery agent (hello, snapshot,
heartbeat). Prefer PlatformIO `m5tab5_ui` for the full radio.

## Relationship

| Piece | Location |
|---|---|
| Driver (portable) | `components/rtl_sdr_v4_esp` |
| This app | `apps/orcsdr-tab5` |
| OrcLink | Separate repo — control plane, not required to run this app |
