# P4 web UI integration status

The P4 web endpoint work is being integrated in dependency order while retaining the existing M5GFX/WebAssembly UI. This page records what the current slice actually serves and what still blocks the complete browser UI. It does not claim dashboard parity.

## Current slice

The server is disabled by default, stores the enable bit in NVS, starts only after Wi-Fi has had an address for 1.5 seconds, and stops or restarts when that address goes away or changes. There is no Ethernet adapter in this P4 target. Host and Origin checks run before each HTTP route. Login uses the PBKDF2/HMAC protocol in `web_auth`; signed requests use the session, monotonically increasing sequence number, request target, and body hash. Login challenges are public and single-use. Auth persists a versioned record containing the derived key, salt, and default-password flag; it never persists the password. Older separate `salt/key/def` records are read and migrated. ESP-IDF 5.5.4's simple NVS handle writes each `set_blob` immediately, so password changes update only the one versioned record; they do not attempt a multi-key transaction. The legacy keys remain unchanged afterward, so downgrading to firmware that does not understand the versioned record can restore the older password. Credential replacement is restricted to a privileged local caller; corrupt records fail closed for web login, with no remote reset or factory-password fallback.

| Request | Current behavior |
|---|---|
| `GET /api/v1/auth/challenge` | Issues nonce, salt, PBKDF2 iteration count, and the factory-password flag. |
| `POST /api/v1/auth/login` | Verifies the browser proof and returns a signed-session identifier and device proof. |
| `POST /api/v1/auth/logout` | Requires a valid signature and expires that session. |
| `GET /api/v1/home` | Read-only Home snapshot, including UI readiness/owner, frequency and separately reported `applied_hz`, filter/mode capabilities, and latest dispatch metadata. Browser audio `volume` and `mute` remain `null`. |
| `POST /api/v1/home?action=...` | Requires a signed session. Allowed actions are queued to the Home UI owner and return `request_id` with `dispatch:"pending"`; queue admission does not mean dispatch or hardware application. Signed `gain_tenth_db` values and `filter_set=0` reset are accepted under Home validation. |
| `GET /api/v1/home/action?id=...` | Reads the current-boot dispatch receipt. A known receipt reports `dispatch_result` from the UI owner; it does not prove the radio applied the requested value. Unknown, pending, and expired IDs are intentionally indistinguishable; only the last eight completed receipts are retained. |
| `GET /api/v1/{receivers,fm,report,tuner,spectrum,time,settings,wifi}` | Explicit `501 endpoint_not_integrated` until current-target adapters are implemented. |
| `POST /api/v1/{receivers/tune,receivers/rate,tuner/gain,tuner/agc,fm,channel,channel/stop,time,freq_set,volume_set}` | Explicit `501 not_integrated`; no placeholder success. Device volume is not a browser control. |
| `GET /` | `503 web_ui_not_packaged` until the parent adds the bundle and login injection. With the bundle, redirect to `/m5`. |

The page's current legacy controls call the unavailable receiver/tuner/FM/channel endpoints, so this slice alone does not make the full UI operational. Browser audio and IQ streaming are not integrated. Do not count those routes or other dashboards as complete.

## Existing UI bundle and provenance

Reuse `apps/orcsdr-web/web` and its page; do not replace the UI. Its WebAssembly build compiles the same `apps/orcsdr-tab5/ui` sources as device firmware. `apps/orcsdr-web/PROVENANCE.md` records the imported source and AGPL-3.0 licensing. M5GFX is fetched externally at tag `0.2.25`, pinned to commit `ad9b814264d4e2000e9f30070002310bbccaffc9`; device M5GFX is a different component version.

The existing P4 bundle embed contract expects these generated files under `apps/orcsdr-web/dist`:

- `app.js.gz`
- `core.js.gz`
- `core.wasm.gz`
- `decoder_worker.js.gz`
- `decoders.js.gz`
- `m5.html.gz`
- `orcdec.js.gz`
- `orcdec.wasm.gz`
- `splash.jpg`

The login script is `apps/orcsdr-p4-hosyond-console/web/orclogin.js`; the page HTML must be passed through `web/inject_login.py` before embedding. The dist directory is generated/ignored and is not present in this worktree, so exact bundle byte sizes and image budget remain to be measured by the parent during packaging. Do not infer flash fit from source sizes.

`apps/orcsdr-web/web/build.ps1` uses Docker image `emscripten/emsdk:3.1.74`, builds the UI and decoder WebAssembly, then packages the bundle. Its dependency on Docker and a pinned external M5GFX checkout is a build/package gate, not part of the firmware module compile.

## P4 wiring proposal for parent

In `apps/orcsdr-p4-hosyond-console/main/CMakeLists.txt`, add only `web_server.cpp`, `web_auth.cpp`, and `web_control.cpp` to the component sources for this slice. The current component already requires `esp_http_server`, `mbedtls`, `heap`, and `nvs_flash`; `nvs_store.cpp` is already compiled from the shared UI source list. Check whether `lwip` needs an explicit component requirement in the chosen ESP-IDF version.

In `main.cpp`, include `web_server.hpp` and call `web_server::start()` immediately after `wifi_app::start()`. The manager starts disabled unless NVS contains `enabled=true`; it needs no board display access and does not own UI state.

The current `serial_cli.cpp` does not implement the `RTL_WEB` commands listed in the broader serial API document. Add a privileged local status/on/off/password path before telling users to enable the HTTP server; the factory-password flag must remain visible until changed. Do not enable the server during boot by default.

## Integrated foundation validation — 2026-11-03

The current P4 app compiles and starts the existing auth/server/control modules.
Settings' existing web-console toggle and authenticated `RTL_WEB STATUS|ON|OFF`
use the same persisted enable service. `RTL_WEB PASSWORD <8..64 characters>` is
an authenticated local setup path, never echoes its argument, and ends web
sessions only after credential persistence succeeds. There is no remote password
reset. The native on-device password editor remains to be integrated.

Server status is copied under a bounded lock rather than exposing its mutable
URL buffer. Enable changes report persistence failure, and manager startup is
guarded against duplicate task creation. These state-boundary checks passed via
`wsl bash tools/test-p4-web-state.sh` (run from the repository directory).
The test uses a host mutex and exercises the production state helper; it does
not emulate the HTTP task or actual NVS storage.

Both focused PowerShell host scripts above passed, including credential failure,
migration, corruption and reboot-resolution cases. Crypto is stubbed in those
persistence tests. ESP-IDF 5.5.4 build and COM32 app flash/hash verification also
passed; local logs are `.local/web-foundation-build.log` and
`.local/web-foundation-flash.log`.

Real HTTP checks passed against the connected LilyGO: PBKDF2/HMAC login and
device proof, unsigned action rejection, Host/Origin rejection, signed request,
sequence replay rejection, invalid signature rejection, Home snapshot and signed
logout. Signed Home step-up/down produced exact dispatch receipts and both
requested/applied frequencies of 96.3 and 96.1 MHz. Device speaker volume/mute
were not browser controls. RTL remained streaming at about 2.4 MS/s afterward;
the audio diagnostic reported existing/startup underruns, so this short check
does not qualify sustained audio continuity. Local evidence:
`.local/web-foundation-device.log`, ending `WEB_FOUNDATION_DEVICE_PASS`.

The test restored the original disabled server setting and Home frequency.
No private password was changed. The browser bundle was not part of that build:
page load, other routes, browser audio/IQ and native password setup remain open.

## Remaining work

The existing `apps/orcsdr-web` sources and P4 login injector are now imported.
`node tools/orclogin_test.js` passed its SHA-256, HMAC and PBKDF2 vectors.
The standalone current-source decoder WASM build passed under the pinned
Emscripten image; `node tools/test_web_decoder_wasm.js` passed ABI lifecycle
and metadata checks for all eight kinds (not protocol replay). The full UI build
failed on the retired `offline_map.cpp`
input; it needs an actual browser adapter for the current OrcMaps architecture.
No obsolete map renderer or mock replacement was introduced, and no generated
UI bundle has been embedded. See `apps/orcsdr-web/PROVENANCE.md`.

1. Port read compatibility routes to current P4 snapshots: receiver inventory and rate, tuner/gain facts, FM/browser-audio status, report, spectrum, and time. Keep unavailable fields `null` or omit them; do not fabricate success.
2. Adapt legacy control endpoints using current `radio` and Home APIs, with driver-derived tune limits and observable driver rejection. Add serial equivalents where user actions do not already have one.
3. Design browser audio and IQ paths against current `radio::set_iq_sink` / `set_aux_iq_sink`, `audio_service`, and DSP ownership. They must remain separate from device speaker volume/mute and must not steal the Home IQ consumer.
4. Package the pinned browser bundle, record per-file byte sizes and total embedded size, then build the P4 application and exercise auth, Home, all implemented legacy routes, page load, audio, and IQ on hardware. Physical browser validation is still open.

## Targeted tests

`powershell -NoProfile -ExecutionPolicy Bypass -File tools/test_p4_web_control_host.ps1` exercises the actual `web_control.cpp` adapter against a stubbed Home queue seam. `tools/orclogin_test.js` validates the browser-side signing implementation; it does not replace a device-side HTTP auth test. After the CMake/startup change, run the P4 component build and then `tools/p4_web_auth_test.py` and `tools/web_home_test.js` against an explicitly approved P4 unit. IQ/audio tests remain gated until those adapters are implemented.
