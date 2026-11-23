# OrcSDR Web UI roadmap

Living tracker for the browser surface of OrcSDR. Started 2026-11-02 on branch `claude/web-in-orcsdr`. Design detail is in `WEB_LAYER_DESIGN.md`, per-action parity in `WEB_PARITY_MATRIX.md`, provenance of the imported build in `apps/orcsdr-web/PROVENANCE.md`.
Status words: **done** (verified, with how), **decided** (owner said yes, not built), **proposed** (needs the owner's yes before anything is built), **open** (a question or unknown).

## Goal and rules (owner)

- OrcSDR is device-agnostic firmware. A screen matters only for its size, resolution, pixel count and touch. With no screen, the browser is the display and control surface for everything OrcSDR can do. Never frame OrcSDR as Tab5-only; never call OrcSDR or the P4 firmware "the node" (OrcNode is a separate project; do not edit its code).
- OrcSDR owns all state. The attached screen, the browser and the serial console are equal surfaces onto it; the browser decides nothing on its own.
- **Parity target = the latest `origin/main`, never a tag.** Name the commit compared in every audit. Last compared: `cdc8181a` (2026-11-02), Home only.
- Do not change how any dashboard works without consulting the owner: audit read-only, show exact before/after, wait for a yes.
- Every control OrcSDR has needs a browser equivalent or a line for the owner to decide. The one deliberate deviation: browser volume and mute stay separate from the device speaker, volume and mute.
- Apply the limits the driver and dongle report (`esp_rtl_sdr`, `radio::tune_min_hz/max_hz`), never our own. Report driver rejections honestly.
- Never merge to `origin/main` unless asked. One extra branch for this work: `claude/web-in-orcsdr` (supersedes `claude/p4-web-ui`). Do not archive or delete branches before 2026-11-16.
- OrcSDR-Headless is import-source only; nobody works there.
- Do not grow `main.cpp`. The driver never depends on OrcSDR. Do not reflash, reboot or clear a unit the owner is watching. Confirm TX wiring before any transmission.
- Boards you may flash for this work: COM29 (Waveshare ESP32-P4-ETH, wired, 192.168.1.69, Nooelec NESDR SMArt v5) and COM8 (Waveshare ESP32-P4-Module-DEV-KIT, Wi-Fi, 192.168.1.198, Blog V4L). Ask first for anything else, including COM31 (Waveshare ESP32-P4-WIFI6-DB) and the Tab5s.
- The owner types passwords in browsers; the factory password is `password`.

## Done

| Item | Evidence |
|---|---|
| Web login (PBKDF2, HMAC-signed requests, 4 sessions with LRU eviction, remember me, log out) via `web/orclogin.js` | `orclogin_test.js`, `p4_web_auth_test.py` on COM29 |
| Browser audio in the firmware (wfm/nfm/am/usb/lsb, port-81 OFA1 WebSocket, page-only volume 0..80, never touches the device speaker, volume or mute) | `web_audio_test.js` all PASS on COM29 and COM8; **not heard by anyone yet** |
| Driver-derived tune range with reported rejections | `web_home_test.js` all PASS on COM29 and COM8 |
| Signed Home route `POST /api/v1/home` and `GET /api/v1/home` (step, step size, span, mode, filter allowed; volume, mute, open_* refused) | same test |
| Web build moved into OrcSDR (`apps/orcsdr-web`), built from `apps/orcsdr-tab5/ui` in place, Docker emsdk 3.1.74, M5GFX pinned 0.2.25 / `ad9b8142`; firmware embeds `apps/orcsdr-web/dist` by default | commits `206f3cdf`..`f4cc147c`, pushed; rebuilt bundle 7 of 9 files identical to the old one (`core.wasm` byte for byte) |
| Mode chip pins Home's mode and a running browser audio session follows it live (2026-11-03) | `tools/web_mode_chip_test.js` all PASS on COM29 (Waveshare ESP32-P4-ETH) and COM8 (Waveshare ESP32-P4-Module-DEV-KIT): one session id, consecutive sequence numbers, socket never closed across 6 picks, board volume unchanged; `web_home_test.js` and `web_audio_test.js` still PASS on both; `p4_health.py` PASS. **Not done:** the chip was not clicked in the browser page; nobody has heard the audio |
| Both boards run the in-repo build; page loads, tune +/- and Start/Stop Browser Audio exercised on COM29 | `p4_health.py` PASS on both; browser pane on COM29 only |

## Next: the owner's decisions pending on the Home route (each needs a yes with exact before/after)

1. **Step size up/down.** Today: client-only cycling in the page. After: OrcSDR's per-band step (`cycle_step_hz`), FIXED on channelized bands, shown from `step_hz` / `step_adjustable`.
2. **Mode chip: DONE 2026-11-03, owner said yes.** AUTO, NFM, AM, WFM, USB, LSB now pin the mode in OrcSDR's Home (`mode_set` through the signed Home route), and a running browser audio session follows without closing its socket (`web_audio::set_mode`, applied by the sink task on the next block; one short click from the filter reset). The device speaker follows the pin too, as it does for a touch. Known limits: the page remembers only the pin it set itself, per band, and forgets it on reload (state sync, decision 5, is not built); the chip is selectable on every band except ADS-B because the page does not have OrcSDR's band profile table.
3. **Filter bandwidth.** Today: page-only. After: `filter_set`. Caveat: on the P4 console's Home it changes only the displayed bandwidth and filter lines, not the DSP. Check in OrcSDR's own source how origin/main applies it first.
4. **`filter_standard`.** Today: a toast. Proposal: `filter_set 0` (the band's standard width), only if the owner agrees and origin/main code confirms it. `open_clock` stays as it is unless the owner says otherwise.
5. **State sync.** One extra small `GET /api/v1/home` per refresh. If the firmware has no home route, the page keeps today's behaviour.
6. **Browser audio as a plain listener on Home's state.** The page sends only Start/Stop and the browser volume/mute; OrcSDR builds the session from Home (frequency, active mode, bandwidth) and follows Home itself when frequency, band or mode changes from any source. The page-side workaround (`/api/v1/freq` for the mode, then `fm?action=start` with `mode=`, `hz=`, `sps=`, `volume=`, `tau=`) goes away. **Answered 2026-11-03:** `Demod::set_mode()` switches live (it resets the filters; the speaker already does this), and the mode part is built (decision 2). Still open for this item: the session ends with `WEB_AUDIO_ENDED receiver_changed` when the frequency or rate changes from outside, so following Home's frequency needs a live target frequency next to the live mode.

Unchanged until the owner says otherwise: tune and TUNE +/-, span, gain, AGC, FM, AM and other screens, volume and mute, login, Start Browser Audio.

## Parity work

- **Per-screen matrix** for FM, AM, CB, Airband, Shortwave, P25, ADS-B, ACARS, LoRa, POCSAG, RF Lab and the rest, against current `origin/main`. Not started; Home rows are verified against `cdc8181a`. Origin/main has 344 commits the branch does not; the web build is **not** resynced to them. Resyncing changes behaviour, so it is its own step with the owner's yes.
- `WEB_PARITY_MATRIX.md` rows beyond Home are from the `v0.5.0-beta.1` snapshot and must be re-verified.

## Architecture direction (audit 2026-11-02; proposed, nothing built)

Findings: origin/main has no P4 app and no web layer; the P4 console compiles the Tab5 `ui/` sources. State for Home sits in the host (Tab5 `ui/main.cpp`, 23,902 lines; P4 `home_app.cpp`). Other dashboards mix state and drawing in 800-1,300 line files unless a `*_model` / `*_core` file exists. The browser UI is the same screens compiled to WebAssembly, with 18 per-screen controllers in `apps/orcsdr-web/web` that re-implement the state `main.cpp` keeps for each screen. `home::Snapshot` / `home::Action` and `dashboard_registry` already show the shape a shared core would take.

Proposed direction, staged, each stage needing the owner's yes:

1. **Core / renderer / platform split**, one dashboard at a time, using the controllers as the specification. Core = state, logic, validated actions, snapshot, no screen or ESP dependency. Gate on Tab5 hardware tests because Tab5 ships from `main.cpp`. Import first, refactor second (the import is behaviour-neutral).
2. **Dashboard manifest** (id, category, required capabilities, supported layout sizes) so the registry offers a dashboard when the hardware is present, with or without a screen. A declarative "user dashboard" layer (manifest plus widgets bound to state) is a later possibility; user-written C++ is not.
3. **One generic signed action endpoint and one state feed** any dashboard uses. The signed Home route is the start.
4. **Parity check** that compares manifests on origin/main with what the web build covers and lists gaps; intentional deviations (device speaker, volume, mute) listed as deviations.
5. **Screen detection at boot, no reflash.** Today the P4 console already falls back to headless when no panel is recognised (`main.cpp` lines 32-48 on the branch). Options for a no-reflash experience: (a) one image per chip family with several board profiles compiled in, probing known panel and touch controllers at boot; (b) per-board images as today; (c) (a) plus a stored profile choice in NVS selectable from the web page. DSI panels are not hot-pluggable, so detection is at boot. When the browser is the display, the device should stop rendering its off-screen sprite and only serve state and actions. **Owner to choose.**
6. **Per-size layouts.** Today all dashboards are written to 1280x720 and scaled (`orcsdr_gfx_compat`). Non-16:9 panels (Waveshare 720x720, LilyGO T-Display P4) need layouts chosen from the manifest; the scaler's behaviour on those aspect ratios is unchecked. A browser window picks the nearest layout class.
7. **Other platforms** (Pi, ARM64, mobile, Apple): the fastest route is a native core plus the browser surface, since the WASM build already proves the UI code builds without ESP.

### Capabilities and TX (owner direction, 2026-11-02)

OrcSDR is about to move from RX-only to TX with the LilyGO T-Display P4 (its branch has a custom TX-capable LoRa dashboard). Direction: boot-time discovery publishes capabilities (`rx_iq`, `lora_tx`, `sdr_tx`, `sd`, ...); a dashboard appears when its capabilities are met (a Heltec on a Tab5 enables LoRa TX). Pluto: model it as a remote radio backend (like `rtl_tcp`), not firmware run on the P4; **open: confirm whether "run our Pluto clone" meant that or OrcSDR hosting it.** Before any TX dashboard: signed requests plus on-device confirmation for TX actions, the Pluto TX safety rules (+19 dBm PA, 20 dB minimum attenuation, tx-guard), confirmed TX wiring before every transmission, and a decision on how the "receive-only" statements in the docs change.

## Open items

- After a driver rejection (e.g. 60 kHz on the Nooelec, floor 100 kHz) Home still shows the requested frequency while the receiver stays at the applied one. **Owner: should Home snap back to the applied frequency?**
- Browser audio has not been heard by the owner. Log in at `http://192.168.1.69/`, press Ctrl+F5, then Start Browser Audio.
- `fm/spectrum` is not served; the Decoders panel shows 404 on `/api/v1/decode/status`.
- Wi-Fi throughput ceiling (~3-5 Mbit/s) not understood; the "Wi-Fi" label on the wired board is cosmetic.
- Two-dongle test on COM31 (Waveshare ESP32-P4-WIFI6-DB): ask before flashing.
- Imported build: `orcdec.wasm` differs from the old bundle by 2 bytes (cause not established); both boards run firmware built before the `decoder_worker.js` line-ending fix (functionally identical); LovyanGFX FreeBSD and font licence texts must be added to `apps/orcsdr-web/THIRD_PARTY_NOTICES.md` before a release that ships `core.wasm`; the browser uses M5GFX 0.2.25, the devices 0.2.27; the page was not opened in a browser on COM8.
- No changelog file exists on this branch; add entries when the branch meets one.

## Tools and build

ESP-IDF 5.5.4. COM29 uses `sdkconfig.defaults;sdkconfig.board-p4-eth;sdkconfig.net-throughput;sdkconfig.silicon-v3` (build dir `build-p4eth-net`); COM8 uses `sdkconfig.defaults;sdkconfig.net-throughput` (`build-modkit-net`). Both need `-DC6_FIRMWARE_BIN=...` and `-DSDKCONFIG=<build dir>/sdkconfig`. Web bundle: `apps/orcsdr-web/web/build.ps1` (Docker). Flash from the build dir: `python -m esptool --chip esp32p4 -p COMxx -b 460800 write_flash "@flash_args"`, then `tools/p4_health.py --port COMxx`. Tests: `tools/web_audio_test.js`, `web_home_test.js`, `orclogin_test.js`, `web_iq_test.js`, `web_iq_cycle_test.js`, `p4_web_auth_test.py`.
