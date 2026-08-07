# OrcSDR Tab5 app (reference radio)

Measured M5Stack **Tab5** (ESP32-P4) radio UI using an on-device RTL-SDR Blog V4.

This application still embeds the V4 USB path in `ui/main.cpp`. The long-term
home for that logic is the standalone component:

**[`components/rtl_sdr_v4_esp`](../../components/rtl_sdr_v4_esp)** (RTL-SDRv4-ESP).

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
