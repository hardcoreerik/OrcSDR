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
