# LIVE monitor integration (2026-11-27)

Integrates the approved two-column LIVE monitor (staged by ChatGPT in 41e01438) into the Digital Voice dashboard. Default density only; the detailed (analysis) LIVE page is unchanged.

## What is wired
- `apps/orcsdr-tab5/ui/digital_voice_live_monitor.hpp`: five panels, RF-level fallback, four popups, `Graphs`, `touch()`.
- `Dashboard::live_service()`: retained widgets (diff slots 1..7), per-panel signature, 120 ms panel cadence, graphs draw only on a new frame, no full-screen clear, popup replaces the right column.
- Actions: HOLD/RELEASE, SKIP, RECORD VOICE (`toggle_save_speech`), popups (`toggle_popup`, UiState::popup). Audio popup holds Auto/Slot 1/Slot 2/Mute; Filter = MONITOR protocol filter; Tune = plan < >, start/stop scan; Options = LOG DATA, detailed screen.
- `Dashboard::set_plot(const PlotData*)` / `Surface::set_plot`: optional real telemetry. nullptr = no producer.

## Not available yet (shown as "--" / "NO ...", never faked)
| Item | Missing producer |
| --- | --- |
| Spectrum + waterfall | No spectrum tap in `dv_radio::offer_internal` (240 kS/s CU8 is available there; a 256-point FFT every ~100 ms would cost well under 1 percent of core 1; measure first). The area shows the real relative RF level trace instead. |
| 4FSK symbol levels | Decoder front ends do not export discriminator samples. |
| SNR, BER, deviation, calibrated RSSI | No producer. Not derived from CRC/FEC counters. |
| Repeater/system directory hint | No directory source; row is built and shows "REPEATER UNKNOWN". |
| Manual frequency entry | Not supported by the app. |

## Verification status
- Host: 5 dashboard suites pass (idle, scanning, hold, discovery, follow, error, popups, touch, stale/new-epoch/valid telemetry, no full clear).
- Firmware builds (app partition 6 percent free) and is flashed to COM17.
- NOT done: native 1280x720 screenshots per state, flicker check by eye, draw-time/worker-backlog measurement. Do not claim acceptance until those exist.
