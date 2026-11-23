# OrcSDR web server and headless conversion: status for the owner (2026-11-02, after the overnight run)

Branch `claude/p4-web-ui` (board work is also on `claude/p4-board-support`). Nothing is merged to main. Details live in `WEB_LAYER_DESIGN.md` (sections 14 to 17), `ESP_RTL_TCP_DESIGN.md` (sections 9 to 13) and `P4_BOARD_PROFILES.md`.

## What runs, on which board
| Board | State |
|---|---|
| COM29, Waveshare ESP32-P4-ETH (wired, NESDR SMArt v5) | Latest build (`build-p4eth-net`). Web server on, rtl_tcp off. Login, control routes, channel IQ (narrow and wide to 2.4 MS/s), rtl_tcp (14 of 14 checks, 2.4 MS/s), 40 min rtl_tcp + web soak, 100-cycle channel soak. |
| COM8, Waveshare ESP32-P4-Module-DEV-KIT (Wi-Fi via C6, Blog V4L, own screen) | Latest build (`build-modkit-net`). Web server on, rtl_tcp off. Login and control tests pass over Wi-Fi; narrow channel passes; wide windows refused as too fast for Wi-Fi (about 5 Mbit/s sustained). |
| COM31, Waveshare ESP32-P4-WIFI6-DB | Not touched since the first web page run (older build). The two-dongle test is still to do. |
| Tab5 (COM17, COM39), COM28 | Not touched. |

The same OrcSDR Web page (the bundled M5GFX/WebAssembly UI) is served from all three P4 boards. Login is `admin` / `password` until changed with `RTL_WEB PASSWORD <new>` over serial.

## Needs you
1. **Type the password into the page** at `http://192.168.1.69/` (COM29) or `http://192.168.1.198/` (COM8), press a control, and see whether it takes effect. I did not enter the password in a browser myself; everything else was tested with scripts.
2. **Listen to a channel** (AM or NFM screen) from the page: browser-side demodulation, audio and the S-meter from the node's IQ stream are untested. The wide window for the decoders (ADS-B, ACARS, ...) is untested in the page too.
3. **The two-dongle test** on COM31 (needs you to plug in the second dongle; image `build-wifi6db-multi` is built, not flashed).
4. Decide whether `sdkconfig.net-throughput` (32 KB TCP windows, 24 sockets, WebSocket support) becomes a default: it costs about 14 KB of internal RAM on COM29 and is required for the channel stream. Tab5 was not touched and its RAM budget is unknown.

## Test scripts (all in `tools/`)
`p4_web_auth_test.py <ip>` (25 login/signature checks), `orclogin_test.js <ip>` (crypto vectors plus control routes), `web_iq_test.js <ip> [hz] [secs] [wide_sps]` (channel IQ), `web_iq_cycle_test.js <ip> [cycles]` (start/stop soak), `p4_rtltcp_test.py <ip>` (rtl_tcp; needs `RTL_TCP ON` over serial). Each prints PASS/FAIL lines and exits non-zero on failure. They log in with the factory password and log out at the end.

## Known open items
- A channel start used to end itself in about 5 % of cycles (the stop frame was the only frame): blocks already queued with the old centre arrived after the retune and looked like "someone else retuned". Fixed: mismatched blocks are ignored for the first 1.5 s. After the fix, 98 to 99 of 100 cycles had frames within 0.4 s (the others had none yet: the retune takes a moment), and 60 of 60 had frames within 1.2 s.
- Wi-Fi through the C6 sustains about 3 to 5 Mbit/s in every test (HTTP download, rtl_tcp, channel); why is not determined.
- The page labels the network "Wi-Fi" on a wired board (cosmetic, inside the bundle).
- `fm/audio` (the page's other WebSocket) is not served; the channel path does not need it.
- rtl_tcp control mode has only been run on COM8; the link budgets in the code are measured on one network.
- The pre-existing Codex research notes, the git "dubious ownership" on imported source folders, and LoRa memory retention (issue 228) are unchanged.
