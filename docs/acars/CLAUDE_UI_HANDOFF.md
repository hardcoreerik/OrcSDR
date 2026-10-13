# ACARS dashboard — Claude handoff (decoder is separately owned)

Branch the UI work **from** `codex/acars-native-decoder`. Do not modify, port, or replace `acars_core.hpp/.cpp`. This branch contains the native core and host smoke test, **not live SDR wiring**.

## Decoder facts
`apps/orcsdr-tab5/ui/acars_core.hpp` declares `orcsdr::acars::Message`, `Stats`, `Decoder` (24 kS/s channel-IQ), `Frontend2400k` (2.4 MS/s CU8) and `FrameDecoder`.
`Message` includes `frequency_hz`, `tail[8]`, `flight[7]`, `label[3]`, `mode`, `acknowledgement`, `block_id`, `has_text`, `end_marker` (ETX/ETB), `corrected`, `corrected_bits`, `raw_bytes`, `text_length`, `text[250]`.
Only CRC-verified frames (including verified repair) trigger callbacks. Text may contain NUL/control bytes: use `text_length` and escape unsafe characters before M5GFX display, never assume `strlen`.
Stats: `sync_candidates`, `crc_ok`, `corrected`, `crc_rejected`, `framing_rejected`, `overflows`, `bits`.

**Not currently provided:** live RF session management, SNR/RSSI, GPS/aircraft position, UTC timestamp, channel activity, archive, message reassembly, multiple channel reception, or application-level message interpretation. These require a later producer/runtime; do not invent.

## UI ownership and exact OrcSDR standards
Target Tab5 **1280x720** with M5GFX / M5Unified. Inspect `pocsag_dashboard.*`, `adsb_dashboard.*`, `airband_dashboard.*`, `dashboard_audio_control.*`, `screen_controller.*`, `dashboard_registry.*` and `navigation_service.*`.
Use `audio_header::draw_brand("ACARS")`, `audio_header::draw_battery`, and the standard shared HOME, MUTE/volume tray, VIS and SETTINGS buttons with their proper hit tests.
**Status-bar reservation:** x=420 y=12 width=446 height=54 (`audio_header::kStatusBar*`); all non-Home dashboards must leave it blank, no ACARS text or meters there. Brand occupies left; battery/buttons x>=866. Reset M5.Display font (`setFont(nullptr)`) before shared header calls after using DejaVu.
Use `screens::Id::acars` and ScreenController transition/draw ownership. Never render in USB/DSP callback or background task. Prefer dirty-region repaints, stable 16/24/40 fonts, and legible finger-size hit targets.

## UI scope
Build modular UI and deterministic host-testable `acars_dashboard_model.*`, with a **fixed, bounded** 12-message/8-aircraft/6-channel-or-preset snapshot (adjust after memory sizing). Avoid large internal-BSS globals: see POCSAG dashboard's lazy PSRAM snapshot allocation. Explicit `demo/live` source flag. Demo fixtures only on user-requested DEMO or dedicated documentation capture, never presented as real RF.
Implement functional LIVE message list/detail, AIRCRAFT grouping, CHANNELS presets/single-current-channel state, ACTIVITY validated counters, ARCHIVE unavailable until adapter exists, SETUP actual preferences. Future multi-channel, reassembly, ADS-B correlation and deep interpretation can appear as clearly unavailable/planned placeholders, not claimed as implemented.

## Integration limits
Add UI modules, UI test, CMake entries, `screens::Id`, registry, navigation, Home dispatch and minimal necessary adapter calls. If screen lifecycle is more than a few lines, use `acars_app.*` outside `main.cpp`.
**Do not enlarge `main.cpp` with screen rendering, fake data, persistent storage, decoder DSP, receiver ownership or scanning.** Do not add an ACARS alternate USB driver. A later receiver adapter will convert decoder callbacks into a bounded queue/snapshot on the UI task.

## Verification
Host UI-model tests, screen ownership checks, header geometry tests, explicit DEMO labeling, navigation/touch tests, build supported firmware if toolchain exists; distinguish host-only from physical Tab5 evidence. Test `bash tools/test-acars-smoke.sh` and review decoder evidence under `docs/acars/`. Logs are saved to `build/acars/smoke.log`.
