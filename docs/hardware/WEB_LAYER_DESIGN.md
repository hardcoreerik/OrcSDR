# Web layer: shared command layer and authentication

Status: design only, 2026-10-30. No code written. P4 console only (`apps/orcsdr-p4-hosyond-console`); nothing here touches a Tab5 file.
Builds on [WEB_API_COMMAND_MAP.md](WEB_API_COMMAND_MAP.md) (which route maps to which command) and [IQ_SINKS_DESIGN.md](IQ_SINKS_DESIGN.md)
(IQ consumers, multi-dongle).

Owner direction (2026-10-29): whatever P4 the dongle is plugged into owns it; the web server is another way to control and view OrcSDR; reuse
the existing radio services and commands, no second receiver manager, Wi-Fi stack or serial CLI.

## 1. What exists (read from the code)

- **Serial CLI** (`main/serial_cli.cpp`, 676 lines): one string-dispatch function `handle_line`, commands print `RTL_*` lines with `printf`.
  Handlers call `radio_service`, `home_app`, `wifi_app`, `sd_card`, `audio_service` directly (no layer between parsing and doing).
- **Serial auth**: `PAIR <32-byte hex>` stores a pairing key in NVS the first time (trust on first use, `PAIR_LOCKED` afterwards);
  `AUTH <16-byte nonce> <HMAC-SHA256(key, "host"+nonce)>` answers `AUTH_OK <HMAC(key, "device"+nonce)>`. The nonce is chosen by the
  client, so a captured exchange can be replayed; fine on a physical cable, not on a network. State is one global `g_authenticated`
  that expires after 10 s without a command. Wi-Fi credentials are set with `RTL_WIFI CONNECT` (auth-gated).
- **Tab5 web console** (`apps/orcsdr-tab5/ui/web_console.cpp`, 533 lines, not used by the P4): port 80 `esp_http_server`, stack 8192, 5 sockets,
  12 URI handlers, mDNS `orcsdr.local`, a whitelisted `POST /api/action` taking a body under 48 bytes, **same-origin check** on
  `Origin` vs `Host`, no passwords, the on-screen text says "LAN console is read-only". `RTL_WEB ON|OFF` is its serial toggle.
- **OrcSDR Web** (`OrcSDR-Headless`, `main/http_api.cpp`, 1,984 lines): port 80, stack 16384, core 0, 96 URI handlers, LRU purge; WebSocket IQ
  on port 81; mutating routes need only the header `X-OrcSDR-Confirm: <action>` (a convention, not a secret), no authentication. Its
  page deliberately refuses to take a Wi-Fi password.
- **Memory today** (P4 console, Wi-Fi up, measured 2026-10-29/30): internal free 163 to 166 KB, largest block 86 to 94 KB; DMA free 124 to 127 KB.
  Any HTTP server, sockets and TLS must be budgeted against that; not measured with a server running.

## 2. Shared command layer

One table of commands; the serial CLI and the HTTP server are two front ends over it.

```
enum class Access : uint8_t { read, control, admin };      // who may call it
struct Caller { Transport via; bool authed; uint8_t session; };    // serial or http
struct Args { const char* positional[4]; KeyValue named[8]; int rx; };     // rx = receiver index, default 0
struct Result { Status status; Field fields[12]; };         // ordered name/value pairs, strings or numbers
struct Command {
  const char* name;                 // "tune", "wifi.scan", ...
  const char* serial_name;          // "RTL_TUNE": the existing spelling, kept
  Access access;
  Status (*run)(const Caller&, const Args&, Result&);       // must not block; same rules as today's handlers
  void (*serial_text)(const Result&);   // byte-identical legacy output for serial
};
```

- **Rules for handlers:** non-blocking, no UI drawing, thread-safe (they already post to atomics and queues: `radio::tune_to`,
  `home_app::request_*`, `wifi_app::request_scan`). A command that takes seconds (Wi-Fi scan, sweep) returns `accepted` and the
  result is read by a status command, as `RTL_WIFI SCAN` / `RESULTS` does today.
- **Access is checked once, in the dispatcher**, before `run`: `read` always allowed; `control` needs an authenticated caller; `admin`
  needs an authenticated caller on serial only (see 3.4). Today each handler calls `authed()` itself, which is how the "some commands
  require auth, some don't" inconsistency in `docs/API_SERIAL_CLI.md` arose; the table makes the choice visible and reviewable.
- **Two renderers**: `serial_text` prints exactly the lines now printed (so `tools/p4_health.py`, `tab5_ft8.py` and anyone's scripts do not
  change); the HTTP renderer emits JSON from the same `Result`.
- **Receiver index everywhere**: every receiver-related command takes `rx=` (default 0), matching the `radio_service` per-receiver API
  from the multi-dongle work.
- **Migration, one command at a time**, read-only first (`status`, `rx.status`, `mem`, `cpu`, `wifi.status`, `sd.status`, `audio.status`),
  then `tune`/`freq`/`volume`, then gain/agc/bias/rate (new serial commands over existing `radio_service` calls), then the rest.
  After each batch: record a serial transcript before and after and require identical output.

## 3. Authentication for HTTP

### 3.1 Threat model
The server sits on a home LAN that also holds unauthenticated devices (the Pluto, other boards). Assume other LAN hosts can read the
traffic and send requests; a browser on the LAN can be tricked into cross-site requests; the connection is plain HTTP (no TLS).
Out of scope: someone with physical access (they have the serial port and can read everything the serial path can).

### 3.2 Tiers
| Tier | Examples | Over HTTP |
|---|---|---|
| read | status, rx status, spectrum, Wi-Fi scan results, decoder events | open when the web server is enabled (the Tab5 precedent), see decision 2 |
| control | tune, gain, rate, volume, open a screen, start/stop a decoder | needs a signed session |
| admin | Wi-Fi credentials, restart, coprocessor update, SD format, `PAIR` | never over HTTP; serial only |

### 3.3 Signed requests, no secret on the wire
Plain HTTP means a bearer token can be sniffed and replayed, and `crypto.subtle` is not available to a page served over HTTP from a
LAN address (not a secure context), so the page carries a small SHA-256/HMAC implementation. Proposal:

1. `GET /api/v1/auth/challenge` returns a **server-issued** random 16-byte nonce `n` (single use, expires in 30 s).
2. The client computes `proof = HMAC-SHA256(web_key, "host" + n)` and `POST /api/v1/auth/login {n, proof}`.
3. The server checks it in constant time, consumes `n`, replies with a session id and `HMAC(web_key, "device" + n)` (the browser can verify the device) and derives
   `session_key = HMAC(web_key, "session" + n)` on both sides. The session key never crosses the wire.
4. Every control request carries `X-Orc-Session`, `X-Orc-Seq` (strictly increasing) and `X-Orc-Sig = HMAC(session_key, seq || method || path || SHA256(body))`.
   The server rejects a seq that is not greater than the last one, a bad signature, or an expired session (idle 5 minutes, hard 1 hour).
   A sniffer sees only signatures bound to one request and one sequence number: no replay, no body tampering, nothing reusable.
5. Failed logins back off (1 s, doubling to 30 s per source IP), at most 2 concurrent sessions.

### 3.4 What is deliberately not allowed over HTTP
`PAIR` (it is trust on first use over a physical cable and would let the first LAN host claim an unpaired device), anything that takes or
changes a Wi-Fi password, restart/clean restart, coprocessor update, SD format, and the `RTL_UI TAP` injector. Consistent with the other
session's page, which already refuses to take a Wi-Fi password.

### 3.5 Web key
`web_key = HMAC-SHA256(pairing_key, "orcsdr-web")`. Deriving it means one pairing secret, no second thing to store, and a leaked web key does not reveal the serial
pairing key. A new authenticated serial command (`RTL_WEB KEY`) prints it once so the owner can paste it into the page, which keeps it in
that browser's local storage. A device that was never paired over serial has no key, so the web layer would offer read tier only.

### 3.6 Browser protections kept from the Tab5 console
Same-origin check on `Origin` against `Host`, and a `Host` allow-list (the device's IP and `orcsdr.local`) against DNS rebinding;
body size caps; `Cache-Control: no-store`; an error return that closes the connection instead of draining an oversized body.

## 4. Server component

`components/orcsdr_web` (P4 build first; the Tab5 keeps its own console until the owner approves a change there).

- `esp_http_server` on port 80, core 0, stack 8192 to start (the other branch uses 16384 for 96 handlers; measure), sockets capped.
- Routes register from the command table (one generic handler that looks up the command, checks access, runs it, renders JSON), plus
  static files for the WebAssembly UI served from flash (the other branch's image is about 2.2 MB; this app is 2.8 MB in a 7 MB slot).
- IQ for browser decoders and RF Lab is a separate WebSocket (port 81 in their design) fed by a **queued sink** on the receiver
  (IQ_SINKS_DESIGN); it adds its own memory and is built after the control path.
- Enable/disable: `RTL_WEB ON|OFF` (already specified for the Tab5 in `docs/API_SERIAL_CLI.md`) and the Settings toggle. The P4 settings
  file currently drops the `web_console_changed` action; hooking it is a P4-only edit. mDNS `orcsdr.local` as on the Tab5.
- Off by default; starts only with Wi-Fi up (or Ethernet, later) as the Tab5 console does.

## 5. First slice (when approved)

1. Command table + dispatcher with the existing read-only commands, serial output byte-identical (transcript diff).
2. `orcsdr_web` serving `GET /api/v1/status`, `/receivers`, `/rx/status`, `/mem`, `/wifi/status` as JSON, read tier only, `RTL_WEB ON|OFF`.
3. Memory/CPU measurement with the server running on COM8, COM29 and COM31 (internal free, largest block, `RTL_CPU`, receiver drops).
4. Then auth (3.3), then control commands, then the WebAssembly page, then the IQ WebSocket.

## 6. Tests

Serial transcript regression; HTTP with curl for every route (valid, bad method, oversized body, missing header); auth: replayed
signature, reused nonce, expired nonce, wrong key, seq going backwards, tampered body, wrong `Origin`, wrong `Host`; two clients at once;
memory before/after enabling; receiver drops with the server under load (a loop of status requests plus a large static file download).
None of these has been run.

## 7. Decisions for the owner

1. **Web key derived from the pairing key** (proposed) versus a separate web password.
2. **Read tier open on the LAN or behind the signature too.** Status contains the SSID, IP and device identity; open matches the Tab5 console, closed is safer.
3. **TLS.** Signed requests make it unnecessary for integrity but it would hide read-tier data; `esp_https_server` has a RAM cost I have not measured.
4. **Admin over HTTP never** (proposed), or allow Wi-Fi setup through a one-time on-device confirmation.
5. **Who owns the page code**: the other session's `platform/web` build, served unchanged, with a small auth shim added.

## 8. First slice: implemented and tested (2026-10-30, COM31 = Waveshare ESP32-P4-WIFI6-DB)

Owner approved the design with the proposed defaults (web key derived from the pairing key, read tier open on the LAN, admin serial-only, no TLS yet).

- `main/commands.hpp` / `commands.cpp`: command table, one access check in `run()`, serial renderer, JSON renderer. Ten read-tier commands moved in:
  `RTL_STATUS`, `RTL_FREQ`, `RTL_SIGNAL`, `RTL_VOLUME`, `RTL_MEM STATUS`, `RTL_RX STATUS`, `RTL_WIFI STATUS`, `RTL_SD STATUS`, `RTL_AUDIO STATUS`, `RTL_UI STATUS`.
  The old handlers were deleted from `serial_cli.cpp`; set-value forms (`RTL_FREQ <HZ>`, `RTL_VOLUME <n>`) stay there. `RTL_JSON <line>` prints a table command as JSON.
- **Serial transcript regression:** 21 lines captured before the change and after, from the same board with a Blog V4 attached: identical
  structure (labels, field names, field order, quoting), values masked. Number formats held (`signal_dbfs` 1 decimal, `fill` 2, `ppm` 0).
- **JSON:** all ten commands parse as valid JSON; `RTL_JSON RTL_TUNE` is refused (not read tier).
- **Health check** (`tools/p4_health.py --port COM31`): PASS (draw 10.6 ms, frame period 50 ms, IDLE1 52.5 %, audio 591 blocks in 2 s, 0 dropped).
- **Memory finding and fix:** the first version kept two static result buffers and a 2 KB JSON buffer in internal RAM. Internal free fell from
  162,955 to 157,483 B and the largest block from 86,016 to 77,824 B. The result and JSON buffers now live in PSRAM; after that, internal free is
  162,747 B and the largest block 86,016 B (baseline 162,955 / 86,016).
- Not done: the web server component, signed-request authentication, control-tier commands, any HTTP route.

## 9. Read-only web server: written and built, not yet run (2026-10-30)

`main/web_server.{hpp,cpp}` (kept inside the P4 console app for now, not a shared component, so no Tab5 build can see it).

- `esp_http_server` on port 80, core 0, stack 8192, 4 sockets, 4 URI handlers, LRU purge, 5 s timeouts. Routes: `GET /` and `GET /api/v1` (index of
  commands with their serial names and access level), `GET /api/v1/<path>?rx=N` for every table command that has an HTTP path: `status`, `freq`,
  `signal`, `volume`, `mem`, `rx`, `wifi`, `sd`, `audio`, `ui`, `web`. JSON comes from the same `Result` as the serial output. Anything else: 404.
- Read tier only. The caller is never authenticated on this path, so the dispatcher would refuse any non-read command (none is registered).
- **Off by default.** The choice is saved in NVS (`orcsdr_web/enabled`). A manager task starts the server only while enabled and Wi-Fi has been
  connected for 2.5 s, and stops it when either goes away. Buffers (result and 2.5 KB JSON) are allocated in PSRAM at start and freed at stop.
- **Controls:** `RTL_WEB` / `RTL_WEB_STATUS` (read) print `RTL_WEB_STATUS enabled= listening= url=`; `RTL_WEB ON|OFF` needs auth. The on-device
  Settings "web console" button now sets the same switch (the P4 settings file used to drop that action).
- **Request checks:** `Host` must be the device's own IP or `orcsdr.local` (rebinding defence); a present `Origin` must be `http://<that host>`;
  `rx=` must be 0 to 3; `Cache-Control: no-store`.
- **Not done:** mDNS (`orcsdr.local` is accepted as a Host but nothing announces it; the mDNS component is a new managed dependency), signed-request
  authentication, control-tier routes, the WebAssembly page, the IQ WebSocket, TLS.
- **Untested:** everything above. Needed before it counts: build the other board profiles, run on a board with the server off then on and measure
  internal free / largest block / `RTL_CPU` / receiver drops, curl every route including bad Host, bad Origin, bad `rx`, unknown path, long URI,
  and several rapid requests during streaming.

### 9.1 Results on COM31 (Waveshare ESP32-P4-WIFI6-DB), 2026-10-30, build `build-wifi6db-c5`, Blog V4 streaming at 2.4 MS/s

- **Off by default and closed when off:** with the server disabled, curl to port 80 is refused. The idle manager task costs about 4 KB of internal RAM
  (158,423 B free against 162,747 B without it).
- **Enabled** (`RTL_WEB ON`, authenticated over serial): listening within seconds, `url=http://192.168.1.44/`. Cost of the running server:
  internal free 147,431 B (-11.0 KB against off), largest block 73,728 B (against 81,920 B).
- **Routes:** `/`, `/api/v1` and all eleven `/api/v1/<name>` routes return 200 with valid JSON. Bad cases: `rx=9` and `rx=abc` 400; unknown path
  and nested path 404; wrong `Host` 403 (`host_rejected`); cross-site `Origin` 403; same-origin `Origin` 200; POST and PUT 405; HTTP/1.0 without `Host`
  403; a 600-character URI 414. (404 for non-API paths and 405/414 come back as plain text from the HTTP library, not JSON.)
- **Load:** 2,031 requests in 40 s from 4 clients (about 50 per second), all 200; latency median 78 ms, p95 96 ms, max 226 ms. Receiver drops stayed 0 and internal
  memory was identical before and after (no leak). Under load: `httpd` 13.0 % of core 0, radio task 47.5 % (45.0 % idle), core 1 idle 43.1 %.
- **Lifecycle:** `RTL_WEB OFF` stopped the server and closed the port, internal free returned to 158,039 B; `RTL_WEB ON` followed by `RTL_RESTART SOFT`
  brought it back by itself after the reboot (saved setting).
- **Not tested:** the other board profiles' builds, the Settings-screen toggle (no screen on this board), multiple server start/stop cycles over hours, IPv6,
  many simultaneous slow clients, a browser rendering the JSON.

## 10. Direction (owner, 2026-10-30): the M5GFX/WebAssembly UI is the default web UI on every OrcSDR board

The UI built in `OrcSDR-Headless` (branch `feat/m5gfx-web-mirror`) is where this work started and currently lives. It is to be served by **all** OrcSDR boards,
headless or with a display: Tab5, Waveshare P4 boards, any future P4 console. It replaces the Tab5 Companion page (`web_console.cpp`) and the JSON-only page at `/`
described in section 9 (that JSON API stays at `/api/v1` as the machine interface). A board with a screen keeps its on-device UI; the browser UI is an addition.

What follows from it:

1. **One shared web component, many boards.** Server, routes, authentication, asset delivery and the compatibility layer live in one component
   (`orcsdr_web`) used by every target. Hardware differences sit behind a small interface (receivers, network info, SD, clock, settings, screen control):
   `radio_service` is the P4 backend. The Tab5 backend (its own radio code) is a separate step that changes Tab5 builds, so it needs the owner's approval and
   on-device tests before anything is merged. The component is built and proven on the P4 first; nothing in it is added to a Tab5 build until then.
2. **One UI source.** The screens are `apps/orcsdr-tab5/ui` compiled natively for devices and to WebAssembly for browsers. The other repo's web build
   (`platform/web`: controllers, stubs, page, Emscripten build) and decoder library (`core/decoders`, `components/orcdec`) have to move into the OrcSDR repo
   (the other session's `docs/MERGE_NOTES.md` lists how). That is a merge into OrcSDR and waits for the owner's authorization.
3. **Assets are a build input, not committed binaries.** The page bundle (about 700 KB gzipped: `core.wasm` 393 KB, `orcdec.wasm` 117 KB, scripts, page, splash)
   is produced by the WebAssembly build and handed to the firmware build by a path (`ORCSDR_WEB_UI_DIR`, like `C6_FIRMWARE_BIN`), embedded in the app image.
   A build without it serves only the JSON index and logs that the UI is not included. Releases must build and include the bundle.
4. **Budget per board.** P4 console: the 7 MB app slot holds it (image is 2.8 MB); a running server costs about 11 KB of internal RAM (measured, COM31).
   Tab5: its app slot size and internal-RAM headroom are unknown here and its internal boot pool is already nearly full, so the Tab5 server is not promised
   until measured.
5. **API compatibility.** The page calls about 25 `/api/v1` routes and expects its own JSON shapes (`receivers[]`, `report` with `eth`/`wifi`, `tuner`, `fm`, `spectrum`,
   `time`, `decode/*`, `channel` plus a WebSocket for IQ). Proposal: an adapter layer inside `orcsdr_web` produces exactly those shapes from the hardware interface, so
   the page runs unchanged; the command-table records stay as the second, simpler API. Unifying the two is later work.

Order of work: (a) P4 headless: embed the bundle, serve it at `/`, add the read-only compatibility routes so the Home screen shows live data; (b) signed-request
authentication and the POST routes (gain, AGC, bias, tune, rate); (c) the IQ WebSocket on queued sinks, then `channel` and `decode`; (d) extract the shared
component; (e) P4 boards with a display; (f) Tab5, with approval and tests.

## 11. Owner decisions and requests (2026-10-30)

- **Default login for step two: `admin` / `password`** (owner decision). Step two builds the login and the control routes on the same server. Concerns recorded for the
  owner, none acted on yet: a published default credential protects nothing against anyone on the LAN who knows it, and the signed-request scheme in section 3 is only as strong as the
  secret it is keyed from. Mitigations to build with it, pending the owner's choice: a persistent "default password in use" banner in the UI, a serial command to change it, throttled failed logins,
  read-only routes staying open as today, and the control tier (including rtl_tcp control mode) refusing to be enabled on a link the owner has not marked as trusted. The pairing key stays the
  serial-side secret.
- **Test the M5GFX web UI on the Waveshare ESP32-P4-ETH (COM29).** This board has no Wi-Fi radio, and the web server today starts only when Wi-Fi is connected, so the test needs two things that
  do not exist yet: an Ethernet driver in the console for the board's RJ45 port (pins are in the board profile: IP101 PHY, MDC 31, MDIO 52, reset 51, RMII clock in on 50, TX_EN 49, TXD0/1 34/35, CRS_DV 28,
  RXD0/1 29/30; documented and from OrcNode, not verified on this board; the PHY chip itself is unconfirmed) and a server start condition that accepts a wired link. The owner will need to connect
  an Ethernet cable to COM29 and tell me when. The Blog V4L dongle used earlier should stay on COM29's USB header. Order: finish step one on the Wi-Fi board first (COM31), then the Ethernet driver (board-support
  branch), then the UI test on COM29.
- **Branches:** board support lives on `claude/p4-board-support`, the web layer on `claude/p4-web-ui` (which contains the board work). Board fixes go on the board branch and are cherry-picked up. Nothing is merged to `main`.

## 12. Step one run on COM31 (Waveshare ESP32-P4-WIFI6-DB), 2026-10-30

Firmware: `claude/p4-web-ui`, build `build-web-ui` (bundle from `F:\Ai\_orcsdr_web_ui\m5gfx-web-mirror-67e535e`, copied unmodified from the other repository at tip `67e535e`), flashed to COM31. Blog V4 streaming at 2.4 MS/s.

- **Serving:** `/`, `/m5`, `/m5/app.js`, `/m5/core.wasm`, `/m5/splash.jpg` return 200 with the right MIME types and `Content-Encoding: gzip` where expected, byte counts equal to the source files (1,934 / 18,199 / 393,406 / 137,898).
- **Data routes:** `receivers`, `fm`, `report`, `tuner`, `time` return valid JSON in the page's shapes (receiver 0 present, state 3, 28 gain steps, 24 kHz to 1.766 GHz range for the V4); `spectrum` returns a 532-byte row.
  First flash: the row was empty because the Home screen's `take_bins` consumes the "new data" flag. Fixed with a non-consuming `peek_bins` (board-support `a8dc860b`); after that the row is live (94.900 to 97.300 MHz around 96.1 MHz, sequence advancing).
  The spectrum levels are the board's relative intensity (not calibrated dBFS), so the shape is right and the level is not.
- **In a browser** (the built-in browser pane): the OrcSDR Home screen renders and updates, with the live spectrum and waterfall, the receiver "#0 Blog V4", node `orcsdr-p4-d7bc55`. The page polls the spectrum several times a second plus receivers, fm, report
  and tuner; all 200. One console error, a 405: very likely the page's `POST /api/v1/time` (it sets the node clock from the browser because the board's clock is unsynced); POST is not built.
- **Load with the page open:** receiver drops 0, radio task 45.5 % of core 1 (45.0 % without), `httpd` 2 % of core 0, internal free 145,451 B (largest 69,632 B).
- **Sink fix (`636991a6`) under cycling:** RF Lab, ACARS and LoRa opened and closed 12 times each, 72 of 72 screen changes acknowledged, no crash, watchdog, quarantine or removal-failure message; the receiver restarted and streamed again after each rate change.
- **Finding (not caused by the sink fix):** after a restart, opening the **LoRa** screen once takes about 16.7 MB of PSRAM and 12.8 KB of internal RAM that are not returned when it closes (PSRAM free 31.4 MB to 14.1 MB, internal 145.8 KB to 131.8 KB). More cycles
  do not grow it further. RF Lab and ACARS return nearly all of theirs (about 0.3 and 0.5 MB stay held). The LoRa screen is `main/screen_lora.cpp`.
- **Not done / not tested:** every route the page uses beyond the six above (decode, channel, wifi/scan, sd, controls, POST), the IQ WebSocket, audio, the decoders and RF Lab inside the browser, a second browser at once, sustained hours, and the P4-ETH (needs the Ethernet driver).

## 13. Web server on a wired link (built, not run), 2026-11-01

For the Waveshare ESP32-P4-ETH (no Wi-Fi radio) the server now starts on either link: the manager serves on the Wi-Fi address when Wi-Fi is connected, otherwise on the Ethernet address
(`eth_app`, board-support branch), after the link has been up for 2.5 s, and restarts if the active address changes. The `Host` check accepts the device's address on either link.
`RTL_ETH_STATUS` / `/api/v1/eth` (read tier) reports compiled, started, link, DHCP address, speed, duplex, MAC, which PHY driver, and why it did not start; the page's `report` route fills `eth.link`
and `eth.ip` from it.

- **Builds:** `build-p4eth-web` (P4-ETH, v3 silicon, Ethernet on, UI bundle embedded; image 3.6 MB, 50 % of the app slot free) and, unchanged in behaviour, `build-web-ui` (WIFI6-DB).
- **Not flashed or run.** First run on COM29 (Waveshare ESP32-P4-ETH) needs an Ethernet cable from its RJ45 to the router. What the first run will tell us: whether the profile pins, PHY address (1) and PHY chip (IP101 first, generic as fallback) are right for this board;
  link speed and duplex; whether DHCP answers; internal RAM left with the EMAC driver (unmeasured); then whether the page loads and streams from the wired address with the Blog V4L on its USB header.

## 14. Step two: login and control routes (built, not flashed), 2026-11-02

Owner decision 2026-10-31: the default login is `admin` / `password`. Written as the design in section 3.3, with the key taken from the password rather than the serial pairing key (so the page needs no pasted key).

- **Module** `main/web_auth.{hpp,cpp}`. The device stores `PBKDF2-HMAC-SHA256(password, salt, 10000)` and the salt in NVS namespace `orcsdr_wauth`, never the password. The first boot creates the factory credential and logs `WEB_LOGIN_FACTORY_PASSWORD`.
- **Routes.** `GET /api/v1/auth/challenge` (single-use 16-byte nonce, 30 s, plus salt, iterations and `default_password`); `POST /api/v1/auth/login` (`nonce=<hex>&proof=<hex>`); `POST /api/v1/auth/logout` (signed).
  Proof, device proof, session key and the per-request signature are defined in the header of `web_auth.hpp`. The signature covers the sequence number, method, the exact target (path and query) and the SHA-256 of the body.
- **Control commands** are table entries with `Access::control`: `RTL_FREQ_SET` (`POST /api/v1/freq_set?hz=`) and `RTL_VOLUME_SET` (`POST /api/v1/volume_set?level=`), same limits as the serial commands (limits now live in `commands.hpp`). They run only for a verified session; the dispatcher refuses them otherwise.
- **Limits.** Two sessions, idle 5 min, hard 1 h; failed logins back off per client address (1, 2, 4 ... 30 s); body capped at 160 bytes; `Host` and `Origin` checks unchanged and applied to POST. Serial: `RTL_WEB PASSWORD <8-64 chars>` (authenticated, never echoed, refuses the factory password, ends every session). `RTL_WEB_STATUS` shows `default_password` and `sessions`.
- **What the factory password means.** It is published, so anyone on the LAN can log in until it is changed. It still blocks a drive-by page in a browser (no session, no signature), it throttles guessing, and nothing sends the password or a reusable token. The page is told `default_password=true` so it can show a banner.
  Admin-tier actions (Wi-Fi credentials, restart, SD format, TAP) stay serial-only whatever the password is.
- **The bundled page cannot use it yet.** Its controls are POSTs with a fixed `X-OrcSDR-Confirm` header (`tuner/gain`, `tuner/agc`, `receivers/tune`, `receivers/rate`, `fm?action=`, `channel`, `time`) and it has no login. Those routes are not built, so every page control returns 405 until the page gets a login
  screen, a small SHA-256/HMAC/PBKDF2 in JavaScript (`crypto.subtle` is unavailable over plain HTTP) and signed requests. That is a change to the page in the OrcSDR-Headless repository and needs the owner's decision.
- **Test:** `tools/p4_web_auth_test.py <ip>` is a reference client and 25 checks (replay, lower sequence, tampered target or body, wrong password, reused nonce, foreign Host and Origin, oversize body, throttle, logout). **Not run**: built (`build-p4eth-web`, image 3.6 MB) but not flashed.

## 15. Web server sockets under load, found on COM29 (Waveshare ESP32-P4-ETH), 2026-11-02

While an rtl_tcp client streamed 2.4 MS/s, about 6 % of HTTP requests from a test script were reset (`ConnectionResetError`, `RemoteDisconnected`); without the stream there were none, and with the browser tab closed there were none (97 of 97).
Cause: the OrcSDR page polls over several connections at once and the server was limited to 4 open sockets with the LRU purge on, so a new client's connection was closed to make room. No log line is written when it happens.
Fix: `max_open_sockets` is 7 when `CONFIG_LWIP_MAX_SOCKETS` is 14 or more (the `sdkconfig.net-throughput` overlay sets 16), otherwise 4 as before. Check: browser page open on COM29, rtl_tcp stream running, 86 of 86 requests answered, 0 failed.
Boards built without the overlay still have 4 sockets and the same weakness; the overlay is not yet a default for any board (it costs about 14 KB of internal RAM on COM29).
Also seen: the bundled page labels the network "Wi-Fi" on a wired board (the label is in the M5GFX status bar; the page only passes a connected flag and an address). Cosmetic, not changed.

**One firmware, three boards (2026-11-02).** The same OrcSDR Web page, from the same embedded bundle, was opened in the browser pane against COM29 (Waveshare ESP32-P4-ETH, wired, NESDR SMArt v5), COM8 (Waveshare ESP32-P4-Module-DEV-KIT, Wi-Fi via the C6, Blog V4L) and earlier COM31 (Waveshare ESP32-P4-WIFI6-DB, Wi-Fi via the C5, Blog V4):
each shows its own receiver name and a live spectrum and waterfall at 2.4 MS/s, and the Module-DEV-KIT's own screen keeps running at the same time. The page's controls still do not work on any of them (see section 14).

## 16. The page's controls and login, built and run on COM29 (2026-11-02)

The bundled page POSTs its controls with no login. Without editing the other repository, the firmware build now adds one line to the embedded copy of `m5.html` (`web/inject_login.py` at configure time, so the bundle on disk is untouched) that loads `/m5/orclogin.js` before the page's own scripts.
`web/orclogin.js` (pure JavaScript SHA-256, HMAC and PBKDF2, because `crypto.subtle` is unavailable over plain HTTP) wraps `fetch()`: a POST to `/api/v1/` waits for a login (a small dialog, user `admin`, a notice while the factory password is in use), then goes out signed as in section 14.
Requests are sent one at a time so the sequence numbers stay in order; an expired session asks again once.

Routes behind the signed session (`main/web_control.{hpp,cpp}`), in the shapes the page sends:
- `receivers/tune?index&hz` (receiver 0 goes through the Home screen, as the serial command does), `receivers/rate?index&sps` (the console runs 960 kS/s and 2.4 MS/s: the nearest is applied and both values are returned), `tuner/gain?index&mode=auto|manual[&tenth_db]`, `tuner/agc?index&on`, `fm?action=volume|start|stop` (start only tunes and sets the volume; `audio_stream:false`), `time?epoch` (sets the node clock from the browser).
- `channel` and `channel/stop` answer 501 `browser_iq_stream_not_available_on_this_node_yet`: the browser audio and IQ WebSockets (port 81) are not built, so the page's channel and decoder features still do not work. Bad values answer 400, unknown routes 404.

Run on COM29 (Waveshare ESP32-P4-ETH): `node tools/orclogin_test.js` passes the five known-answer tests (SHA-256, HMAC including a long key, PBKDF2 RFC 7914; 10,000 PBKDF2 iterations take about 50 ms in Node) and, against the board, a login and signed control requests from the JavaScript implementation: tune, out-of-range tune refused, volume, gain auto, channel refused honestly, clock set, unknown route.
In the real page in the browser pane, pressing a control on a node that is not logged in shows the login dialog with the factory-password notice. **Not done:** typing the password into the page and watching a control take effect. I did not enter the password into a browser form myself (it is a LAN address, not a local development host); that last step is for the owner.
Other unverified: the dialog on a phone-size screen, many rapid control presses, the `time` route (the page sends it on load) after a login in the browser, behaviour when two browsers log in (two sessions are allowed).

**COM8 (Waveshare ESP32-P4-Module-DEV-KIT, Wi-Fi via the C6), same build:** `node tools/orclogin_test.js 192.168.1.198` (9 board checks plus the 5 known-answer tests) and `tools/p4_web_auth_test.py` (25 checks) both pass over Wi-Fi. The Module-DEV-KIT's own screen kept running throughout.
COM8 is flashed with this build; rtl_tcp is off; the web server is on.

## 17. Channel IQ to the browser (built and run on COM29, 2026-11-02)

The page's AM, NFM and SSB listening takes a ~300 kS/s IQ window over a WebSocket and demodulates it in the browser (`channel` POST, then `ws://<node>:81/api/v1/fm/iq`). `main/web_iq.{hpp,cpp}` serves it:
- **`POST channel?index&hz`** (signed session): parks the receiver on a window whose centre is `hz - rate/6` (the channel sits a sixth of the rate above the DC spike, as the page expects), waits for the retune, and returns `{hz, center_hz, sps, offset_hz, bits:8, ws_port:81}`.
  `center`/`rx_sps` (the wide window the decoders want) answer 501. The receiver must be streaming at 2.4 MS/s (decimate 8, 300 kS/s out) or 960 kS/s (decimate 3, 320 kS/s out), otherwise `receiver_rate_not_supported_for_channel`.
- **Decimation:** a third-order CIC filter per component on a queued sink (`webiq` task, core 0, 256 KB PSRAM queue); output is 8-bit unsigned IQ, 512 pairs per WebSocket frame with the page's 28-byte header (`OIQ1`, session, sequence, first sample, rate, frame count, playing flag, bits). The page does its own mixing, filtering and demodulation, so nothing else runs on the node. CIC droop at the channel (+50 kHz of a 300 kHz band) is about 1.2 dB.
- **Who may listen:** a second HTTP server on port 81 with a pre-handshake check: the request must be same-site (`Host` allow-list, `Origin` host equal to `Host` host, the port may differ), come from the address that made the signed `channel` call, within 30 s, and the ticket is single use. Anyone else gets a real 403, never a WebSocket.
- **Ending:** `channel/stop`, a new `channel` (one listener slot), the web server stopping, or another owner changing the receiver's centre or rate (a stop frame with `playing=0` is sent). On stop the receiver is retuned to where it was, unless someone else moved it meanwhile.
- **Needs** `sdkconfig.net-throughput` (HTTP server WebSocket support and its pre-handshake hook, 24 lwip sockets, 32 KB TCP windows). Builds without it answer `browser_iq_stream_not_built_into_this_firmware`.

**Run on COM29 (Waveshare ESP32-P4-ETH, wired), NESDR SMArt v5, FM broadcast at 96.1 MHz** with `tools/web_iq_test.js`: 15 checks, eight runs in a row, all pass. The WebSocket is refused before a signed request and after the ticket is used; 587 frames/s for 3 to 30 s, every frame well formed at 300,000 S/s, sequence numbers consecutive, one session id;
a spectrum of the received frames shows a strong peak (peak over median 300 to 800) within ±40 kHz of the expected channel position (an FM station's instantaneous frequency moves; this is a sanity check, not a measurement); `channel/stop` closes the socket and no frames follow; the receiver returns to 96.1 MHz.
Cost while streaming: the `webiq` task 29 % of core 0, the radio task 53 % of core 1 (the console screen task 22 % of core 0 as well), internal RAM free 116 KB (largest block 38.9 KB; 118 KB / 43 KB with the servers idle).

Bugs found and fixed while building it (all would bite anyone copying the pattern):
- esp_http_server completes the WebSocket upgrade before calling the handler, so a refusal sent from the handler arrives on an already open WebSocket; the check moved to `ws_pre_handshake_cb` (needs `CONFIG_HTTPD_WS_PRE_HANDSHAKE_CB_SUPPORT`).
- A custom `close_fn` replaces the server's own `close()`: without a `close(fd)` in it every WebSocket leaked one lwip socket and the node stopped accepting connections after about four test runs (`accept` error 23). Fixed; 16 sockets were not enough for two servers plus rtl_tcp, 24 are set.
- `issue_challenge` refused when four nonces were outstanding, which lets anyone lock the owner out of login for 30 s by asking for four challenges; it now replaces the oldest nonce instead.
- Test scripts must log out: two sessions fill up otherwise (`sessions_full`).

**Not done / unverified:** the page itself has not been driven through a login and a channel (it needs the password typed in a browser, which only the owner does), so browser-side demodulation, audio playback and the S-meter from this stream are untested; SSB and the wide-window decoders; `fm/audio` (the page's other WebSocket) is not served (the channel path needs no node audio);
a Wi-Fi board (COM8) carries 300 kS/s of 8-bit IQ as 4.8 Mbit/s, which is right at that link's measured sustained rate (ESP_RTL_TCP_DESIGN section 11); behaviour when two browsers want a channel (the second replaces the first).

**Update, same night: frames and priorities, and the Wi-Fi board (COM8, Waveshare ESP32-P4-Module-DEV-KIT).**
- Frames are now 2048 IQ pairs (4 KB, 146 per second at 300 kS/s) with `TCP_NODELAY` on the socket; `tools/web_iq_test.js` expects that size. COM29 (wired): three runs of 15 checks, 0 failed, 146 frames/s each; `orclogin_test.js` and `p4_web_auth_test.py` also 0 failed on the same image.
- **The first Wi-Fi run on COM8 delivered only about 10 % of the frames** (59 frames/s of 586, about 0.5 Mbit/s) and the stream was chopped. Cause: the stream task ran at priority 3 on core 0, below the screen task (priority 4, which on a board with a display uses 25 to 75 % of core 0), so it was starved; a starved task shows a small CPU percentage, which looks like waiting on the network.
  With the stream task at priority 5 the same 300 kS/s channel arrives complete over Wi-Fi: 146 frames/s, 15 of 15 checks, a 20 s run clean (4.8 Mbit/s of payload, which is the top of what that link has carried in any test). The `webiq` task takes 35 % of core 0 there, the screen task 27 %, the radio task 69 % of core 1.
- The rtl_tcp sink had the same priority 3 and is now 5 too. Re-measured over Wi-Fi on COM8: 251 kS/s of a 2.4 MS/s stream (4 Mbit/s) before and after, a plain HTTP download 3.1 Mbit/s: so for rtl_tcp the ceiling really is the link (about 3 to 5 Mbit/s in these tests), and the earlier conclusion in ESP_RTL_TCP_DESIGN section 11 stands, but priority starvation is a second thing to rule out on any board with a screen.
- While a slow link blocks `push()`, the rtl_tcp session task waits for the same lock and handles the client's commands late (a test that checks the counters 1 s after sending commands fails over Wi-Fi). Counters should move to atomics; not done.

**Wide window for the browser decoders (2026-11-02).** `POST channel?index&hz&center&rx_sps` now serves the window the page's decoders ask for: centred on `center`, at `rx_sps` (the receiver's rate divided by a whole number up to 16; 2.4 MS/s gives 2.4, 1.2, 0.8, 0.6 MS/s and so on), `offset_hz` as the signed distance of `hz` from the centre.
At the receiver's own rate the bytes are passed through unchanged; otherwise the same CIC decimator is used. A rate the link cannot carry is refused (`link_too_slow_for_this_rate`; wired 85 Mbit/s, Wi-Fi 5 Mbit/s, the figures from the rtl_tcp server); a rate the receiver's rate does not divide into is refused (`rate_not_reachable_from_the_receiver_rate`).
Run on COM29 (Waveshare ESP32-P4-ETH, wired): 1.2, 0.8, 0.6 and the full 2.4 MS/s windows all arrive complete (586, 391, 293 and 1,173 frames/s of 2048 pairs; 38 Mbit/s at 2.4 MS/s), consecutive sequence numbers, one session, strong spectral peak in the window; 0 failed in each run. On COM8 (Waveshare ESP32-P4-Module-DEV-KIT, Wi-Fi) a 1.2 MS/s window is refused as `link_too_slow_for_this_rate` and the narrow channel still passes.
Not checked: that the page's decoders (ADS-B, ACARS, and the others that use the wide window) actually decode from this stream in the browser, and the 8-bit quantisation after the CIC filter on a weak signal.
