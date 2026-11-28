# LIVE monitor integration (2026-11-27)

Integrates the approved two-column LIVE monitor (staged by ChatGPT in 41e01438) into the Digital Voice dashboard. Default density only; the detailed (analysis) LIVE page is unchanged.

## What is wired
- `apps/orcsdr-tab5/ui/digital_voice_live_monitor.hpp`: five panels, RF-level fallback, four popups, `Graphs` (with a PSRAM offscreen `LGFX_Sprite` for the 4FSK symbol plot on `ESP_PLATFORM`), `touch()`.
- `Dashboard::live_service()`: retained widgets (diff slots 1..7), per-panel signature, 120 ms panel cadence, graphs draw only on a new frame, no full-screen clear, popup replaces the right column.
- Actions: HOLD/RELEASE, SKIP, RECORD VOICE (`toggle_save_speech`), popups (`toggle_popup`, UiState::popup). Audio popup holds Auto/Slot 1/Slot 2/Mute; Filter = MONITOR protocol filter; Tune = plan < >, start/stop scan; Options = LOG DATA, detailed screen.
- `Dashboard::set_plot(const PlotData*)` / `Surface::set_plot`: wired to `State::plot` in `digital_voice_app.cpp`.
- DMR Signal Diagnostics (`dv_radio::signal_diag()`, `d1_bridge::Receiver::set_symbol_sink`, `dv_voice::Engine` Golay BER):
  - **DEV**: DMR outer `±3` 4FSK deviation (`3 * deviation_per_level_hz`) from `d1_frontend`.
  - **SNR**: DMR 4FSK slicer/constellation SNR (MER from `deviation_per_level_hz` and `decision_rms_hz`) once settled (`>= 102` locked symbols).
  - **BER**: rolling pre-FEC BER from `orcsdr_vocoder` Golay(24,12)+Golay(23,12) during voice, falling back to `d1_protocol` Golay(20,8)+BPTC(196,96) on integrity-valid DMR data bursts.
  - **4FSK symbol levels**: last 96 CFO-removed discriminator samples against measured `±1`/`±3` reference lines (`d1_bridge::SymbolSink`).
  - **CRC FAIL & FEC**: real DMR BPTC-valid RS/CRC failures (`s.crc_failures`) and corrected FEC bits (`s.fec_corrected`).

## Not available yet (shown as "--" / "NO ...", never faked)
| Item | Missing producer |
| --- | --- |
| Spectrum + waterfall | No spectrum tap in `dv_radio::offer_internal` (240 kS/s CU8 is available there; a 256-point FFT every ~100 ms would cost well under 1 percent of core 1; measure first). The area shows the real relative RF level trace instead. |
| Calibrated RSSI (dBm) | No calibrated dBm producer; `RF REL` shows the channelizer's relative level in dB instead. |
| NXDN SNR / BER / DEV / symbols | `orcsdr_nxdn` frontends do not export symbol residuals or outer deviation yet. |
| Repeater/system directory hint | No directory source; row is built and shows "REPEATER UNKNOWN". |
| Manual frequency entry | Not supported by the app. |

## Verification status
- Host: Release + ASan/UBSan suites pass (`tests/digital_voice_dashboard_preview` 5/5, `tests/digital_voice_live` 7/7, `components/orcsdr_dmr`, `tests/dmr_protocol_conformance`).
- Firmware: native ESP-IDF 5.5.4 build verified (`apps/orcsdr-tab5`).
- NOT done yet for Signal Diagnostics wiring: on-device flash, native 1280x720 screenshots, flicker check by eye, draw-time/worker-backlog measurement on COM17. Do not claim hardware acceptance until those exist.
