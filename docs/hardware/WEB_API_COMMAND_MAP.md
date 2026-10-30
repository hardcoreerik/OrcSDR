# OrcSDR Web API to OrcSDR command map

Status: analysis only, 2026-10-29. Nothing here is implemented. It maps the HTTP routes of the other session's firmware
(repo `OrcSDR-Headless`, branch `feat/m5gfx-web-mirror`, tip 67e535e, `main/http_api.cpp`) onto what the P4 console
(`apps/orcsdr-p4-hosyond-console`) already has, so the merge reuses `radio_service` and the existing command layer
instead of adding a second receiver manager, Wi-Fi stack or serial CLI.

Design rule (owner, 2026-10-29): the P4 device the dongle is plugged into owns it; `radio_service` is the only code that
talks to `esp_rtl_sdr`; the web server is another way to control and view OrcSDR.

## What exists on this side

- `radio_service.hpp`: `start`, `tune_to`, `set_sample_rate`, `set_sample_rate_override`, `gain`/`set_gain_auto`/
  `set_gain_manual`, `set_rtl_agc`, `set_bias_tee`, `set_ppm`, `set_tuner_bandwidth`, `info`, `snapshot`, `level_dbfs`,
  `take_bins` (256-bin FFT), and **two** IQ consumers: `set_iq_sink` (audio) and `set_aux_iq_sink`
  (RF Lab, ACARS, LoRa, `iq_tap`). One dongle, one instance.
- `home_app.hpp`: `request_frequency`, `request_volume`, `request_sound`, `request_span`, `request_open`, `request_home`,
  `inject_tap`, `open_screen`, `status`.
- `serial_cli.cpp` (auth-gated by the pairing key): `RTL_STATUS`, `RTL_FREQ`, `RTL_TUNE`, `RTL_SIGNAL`, `RTL_VOLUME`,
  `RTL_RESTART SOFT|CLEAN`, `RTL_SPAN`, `RTL_CPU`, `RTL_PROF`, `RTL_BENCH`, `RTL_SD STATUS|CHECK`,
  `RTL_WIFI STATUS|RESULTS|SCAN|CONNECT|DISCONNECT|POWER|FORGET|C6_UPDATE|LINKLOSS`, `RTL_USB_GUARD STATUS|RESET`,
  `RTL_AUDIO STATUS|TONE`, `RTL_UI STATUS|OPEN|TAP`, `RTL_SCREEN_STATUS`, `RTL_MEM STATUS`, `FT8 SNAP`.
- The Tab5 command spec (`docs/API_SERIAL_CLI.md`) is larger. It already defines `RTL_WEB ON|OFF` / `RTL_WEB_STATUS`
  (line 203) and per-decoder commands (`RTL_POCSAG_*`, `RTL_CB_*`, `RTL_ACARS STATUS`, presets, RDS, recording). The P4 console
  implements a subset, so the web toggle already has a named command to bind to.

## Map

Key: **reuse** = the call exists, only an HTTP adapter is needed. **wrap** = `radio_service` or `home_app` has the function but there is no command yet.
**gap** = new work. **decide** = OrcNode-only feature, owner's call.

### Receiver control

| Web route | P4 equivalent | Class |
|---|---|---|
| `GET /api/v1/tuner` | `radio_service::gain()`, `info()`, `snapshot()` (caps, ppm, bandwidth list, gain ladder) | wrap |
| `POST /api/v1/tuner/gain` (`tenth_db`) | `set_gain_manual`; no serial command | wrap |
| `POST /api/v1/tuner/agc` | `set_gain_auto`, `set_rtl_agc`; no serial command | wrap |
| `POST /api/v1/tuner/bias` | `set_bias_tee`; no serial command | wrap |
| `POST /api/v1/receivers/tune` | `RTL_FREQ <HZ>` / `RTL_TUNE <BAND> <HZ>` (`tune_to`) | reuse |
| `POST /api/v1/receivers/rate` | `set_sample_rate` (960 kS/s or 2.4 MS/s only; their UI may ask for other rates); no serial command | wrap |
| `GET /api/v1/receivers` | `RTL_STATUS`, `snapshot()`; **one** receiver only | gap for several |
| `POST /api/v1/receivers/stop` | `RTL_RESTART`/UI close; no pure "stop streaming" command (Tab5 spec has `RTL_STOP`) | gap |
| `GET /api/v1/spectrum` | `take_bins` (256 bins) | wrap |
| `GET /api/v1/waterfall` | UI waterfall in `home_app` is drawn, not exported | gap |

### IQ and audio

| Web route | P4 equivalent | Class |
|---|---|---|
| `GET/POST /api/v1/channel`, `POST /channel/stop`, WebSocket on port 81 (300 kS/s IQ for the browser) | no network IQ out; only `set_iq_sink`/`set_aux_iq_sink` (two slots, aux claimed by RF Lab, ACARS, LoRa, `iq_tap`) | gap: needs a registered-sinks list |
| `GET /api/v1/fm`, `POST /api/v1/fm`, `/fm/stream`, `/fm/spectrum`, `/fm/iq/status`, `/fm_iq_worker.js` | `RTL_VOLUME`, `request_volume/sound`, `audio_service` PCM; no stream out | wrap / gap |
| `GET /api/v1/audio`, `POST /audio/test` | `RTL_AUDIO STATUS`, `RTL_AUDIO TONE` | reuse |

### Decoders

| Web route | P4 equivalent | Class |
|---|---|---|
| `POST /api/v1/decode/start`, `/stop`, `GET /status` | decoders live inside the screens (ADS-B, ACARS, FT8, SSTV, P25, POCSAG, LoRa). Headless mode still renders them into the scaled sprite, so opening the screen starts the decoder (`RTL_UI OPEN`, `request_open`). A decoder with no screen has no start command | wrap by opening the screen; gap for screenless |
| `GET /api/v1/decode/events`, `/aircraft`, `/stats`, `/image` | each screen holds its own event store; no common event ring or JSON export | gap |
| `POST /api/v1/decode/ingest` (browser events pushed to the node) | none | gap |
| `POST /api/v1/decode/inject` | test path only; keep out of production builds | decide |
| `POST /api/v1/decoder` | selects node/browser placement | gap (follows the placement design) |

### System

| Web route | P4 equivalent | Class |
|---|---|---|
| `GET /api/v1/status`, `/health`, `/report`, `/logs` | `RTL_STATUS`, `RTL_MEM STATUS`, `RTL_CPU`, `RTL_SIGNAL`, `RTL_WIFI STATUS`; no log ring export | wrap |
| `GET/POST /api/v1/wifi/scan` | `RTL_WIFI SCAN` / `RESULTS` | reuse |
| (Wi-Fi join, forget, power) | `RTL_WIFI CONNECT|FORGET|POWER`; their page deliberately refuses to take a password over HTTP | reuse, keep refusal |
| `GET/POST /api/v1/time` | clock lives in the Tab5 settings clock; no P4 serial command | gap |
| `GET /api/v1/sd` | `RTL_SD STATUS` | reuse |
| `POST /api/v1/reboot` | `RTL_RESTART SOFT|CLEAN` | reuse |
| Settings toggle (device) | Tab5 spec `RTL_WEB ON|OFF`; the P4 `screen_settings.cpp` drops `web_console_changed` today | wrap |

### OrcNode-only features

`GET /api/v1/anomaly`, `/detector`, `/baseline`, `/events`, `POST /ml/probe`, scanner routes (`/scanner`, `/scanner/start|stop|profile`).
These belong to the anomaly/scanner product (esp-dl, `scan_engine`, `detector`) and have no OrcSDR counterpart. Owner decides whether any of
them ship in OrcSDR Web. Not needed for the first integrated build.

## Conflicts this map exposes

1. **Auth.** Serial commands need the pairing key; the web routes need only a confirm header (`X-OrcSDR-Confirm`, a convention, not a secret)
   and the server has no authentication. A web adapter on the same handlers must not be weaker than the serial port.
2. **Handlers are not shared.** `serial_cli.cpp` calls `home_app`/`radio_service` and prints strings. A common command layer (functions
   returning a struct, with serial printing and HTTP JSON as two renderers) avoids a second CLI. That refactor is P4-only.
3. **Multi-receiver.** The other branch's `receiver_manager` handles several dongles; `radio_service` is a single instance. Keeping the
   multi-receiver capability means `radio_service` becomes an instance per dongle (device index in every call), not a separate manager.
4. **IQ consumers.** Two fixed slots against many wanted consumers (audio, RF Lab, ACARS, LoRa, browser channel, node decode sessions, a
   future network IQ output). A registered-sinks list with per-sink rate/decimation needs and a "must not block" contract fixes it.
5. **Decoders in screens.** On the P4 the decoders run as screens; the other branch runs them as sessions without a screen. Reusing the same
   screens means a decoder is started by opening its screen (works headless), but two browsers or a browser plus a display would fight for
   the one active screen. Needs a decision before any code.

## Suggested first slice (not started)

Read-only and idempotent routes first, all through the existing calls: `status/health/report`, `tuner` (GET), `receivers` (GET),
`spectrum`, `wifi/scan`, `sd`, `audio`; then `receivers/tune` and `tuner/gain|agc|bias` with the same auth as serial. The IQ channel,
decode sessions and multi-receiver come after the sinks list exists.
