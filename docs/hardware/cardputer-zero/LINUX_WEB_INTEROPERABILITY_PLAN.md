# Use the existing OrcSDR Web interface on Linux

Status: **design/integration plan, not a running Linux HTTP server**. Review base: `hardcoreerik/OrcSDR-Headless`, `feat/m5gfx-web-mirror` at `67e535e3a8562709d190194363f7d81ce77fe2f3` (2026-10-28).

## Actual Web product to preserve

The existing Web implementation is substantial; do not recreate it as a separate Cardputer dashboard.

- Browser sources: `platform/web/app/{m5.html,app.js,decoders.js,decoder_worker.js,splash.jpg}`.
- Compiled Emscripten screen and decoder artifacts: `platform/web/dist/core.{js,wasm}` and `orcdec.{js,wasm}` produced using `platform/web/build.sh` and `core/decoders/wasm/build.sh`. Firmware serves compressed `main/m5/*` resources. Keep their source pin/ABI and build provenance.
- Current HTTP router: `main/http_api.cpp` (ESP-IDF-specific). It serves `/` and `/m5` as the OrcSDR browser shell; `/classic` serves the original console. The Linux server must **reimplement the platform adapter**, not compile ESP-IDF HTTP server code on Linux.
- Key read routes actually present: `/api/v1/health`, `/api/v1/status`, `/api/v1/receivers`, `/api/v1/report`, `/api/v1/fm`, `/api/v1/tuner`, `/api/v1/spectrum`, `/api/v1/decode/status`, `/api/v1/decode/events`, `/api/v1/decode/aircraft`, `/api/v1/decode/stats`.
- Relevant write routes: `/api/v1/receivers/tune`, `/api/v1/receivers/rate`, `/api/v1/tuner/gain`, `/api/v1/tuner/agc`, `/api/v1/tuner/bias`, `/api/v1/channel`, `/api/v1/channel/stop`, `/api/v1/decode/start`, `/api/v1/decode/stop`, `/api/v1/decode/ingest`, `/api/v1/fm`, plus scanner and system routes. The client uses `X-OrcSDR-Confirm` headers today; **these are NOT authentication**.
- The actual browser client uses WebSockets at `:81/api/v1/fm/audio` (PCM) and `:81/api/v1/fm/iq` (CU8), not simply `/api/v1/fm/stream` on the normal HTTP port. This two-port/topology assumption must be made configurable or emulated by Linux routing. Cross-origin and secure WebSocket settings must be tested.
- `core/decoders/include/orcdec.h` in Headless defines a useful common C ABI and `docs/decoders/EVENT_SCHEMA.md` documents decoder events. Headless currently vendors copies of OrcSDR decoder sources. The Cardputer target deliberately links original OrcSDR sources and should consume a **canonical** shared `orcdec` tree when Agent 1 reconciles it.

## Integration phases

1. **Static-asset parity** — produce/retrieve build-verified Web artifacts from the exact Headless source pin; serve unchanged files with correct MIME, gzip and cache headers from a Linux file directory. Test `/`, `/m5`, modules, workers and WASM loading in modern desktop/mobile browsers. Do not copy large binary assets into the Cardputer C++ sources.
2. **Radio-service contract** — bind a Linux receiver session, authoritative receiver generation, device capabilities, tuning/readback, sample rate, gain/AGC and errors to Agent 2's versioned Radio Service semantics. Implement the existing JSON GET endpoints using a small Linux HTTP adapter. Unit-test response fixtures against Web's actual `app.js` accessors. Make unsupported controls explicit rather than claiming they work.
3. **Live data** — single receiver owner: `librtlsdr` producer -> bounded, discontinuity-aware IQ queue -> DSP/decoders and optional PCM/IQ WebSocket fan-outs. No client may own the USB device directly. Drop slow subscribers with per-client accounting instead of blocking RF capture. Respect sample format, exact rate and session-generation changes.
4. **Decoder placement** — bridge native OrcSDR decodes to existing event endpoints and optional in-browser `orcdec.wasm`. Normalized event sequence, frequency, timestamp, CRC correction, source provenance and reset semantics must match the shared Radio Service and `EVENT_SCHEMA.md`. Run the existing `tests/decoders/parity.py`/placement harness against Linux and reference Web before declaring parity.
5. **Security gate before LAN** — read-only mock mode may bind **127.0.0.1** for local developer tests; any remote-control server accessible off-device requires verified authentication, per-session permissions, CSRF/origin checks, WS auth, safe credentials/config and TLS strategy. Coordinate with Agent 3's Web security work. Confirm headers are not a security boundary. No open unauthenticated Wi-Fi hotspot or public web endpoint.
6. **Packaging and field operation** — Linux service under unprivileged account, graceful USB/WS teardown, `systemd` integration, launch/diagnostic logs and non-root Debian packaging via official CardputerZero/AppBuilder. Keep the Web UI usable even if LVGL LCD shell is absent.

## Acceptance (all pending unless otherwise evidenced)

- [ ] HTML, JS, WASM and worker assets load unchanged, with matched hashes vs source build.
- [ ] All required read endpoints conform to Web's actual field names, sample formats and error semantics.
- [ ] Tune, rate, gain, channel, decoder and stop operations round-trip with real receiver generation/status.
- [ ] Live streams on phone/tablet/desktop recover from slow consumer, retune, Wi-Fi loss and SDR unplug without stale data or false loss counters.
- [ ] Authenticated LAN client and unauthenticated client exercise every write path and WS handshake with appropriate reject/allow results.
- [ ] Original browser dashboard (not a mock second implementation) renders/controls live Linux SDR sessions.
- [ ] CPU, RAM and USB/ethernet contention measured with a real Cardputer; optional 320 x 170 LVGL local control shell works independently.

**Important:** Full Web dashboard availability is the eventual goal. The present Linux CLI and decoder/queue tests **do not** implement an HTTP server or Web interoperability by themselves.
