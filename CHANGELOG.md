# OrcSDR changelog

This is the **project-level release and current-source index**. It links the authoritative, version-specific release notes and feature-level development records rather than rewriting historic test results. For a current source/receiver matrix see [PROJECT_STATUS.md](PROJECT_STATUS.md), and for on-device decoders see [Native Decoder Inventory](docs/NATIVE_DECODER_INVENTORY.md).

## Unreleased: OrcSDR Web and OrcSDR P4 Core landing branch, in progress — 2026-11-08

**Not a release.** Work on `claude/orcsdr-web-landing`, not on `origin/main`.

- **OrcSDR P4 Core** (`apps/orcsdr-p4-core`): the firmware for ESP32-P4 boards, built from the shared OrcSDR screens, with OrcSDR Web in the image. Built for the Waveshare ESP32-P4-ETH and ESP32-P4-WIFI6-POE-ETH.
- **OrcSDR Web** (`apps/orcsdr-web`): the browser page, the signed-request login and the port-81 IQ and audio sockets. The page now compiles OrcMaps; no map file is supplied to the browser yet, so maps show "NO BASEMAP".
- **Waveshare ESP32-P4-ETH (COM29):** health check and the browser protocol tests pass on Ethernet. **Waveshare ESP32-P4-WIFI6-POE-ETH (COM28):** boots and finds its IP101 PHY and the ESP32-C6; link and web tests not yet run.
- **Browser basemap delivery:** the board serves `/sd/orcmaps/basemap.pmtiles` to the page in signed 32 KiB chunks (`POST /api/v1/map/status` and `/map/read`); the page writes it into the WebAssembly virtual filesystem and rescans, with a status line (no SD card, no file, loading, loaded). Tested on COM28 for the absent path only (`tools/web_maps_test.js`): no board has a card with a basemap, so delivery of real data and the browser rendering of a map are untested.
- **Not verified:** the P4 map service with a real basemap, browser audio heard by a person, any other board.

## Unreleased — rtl_tcp remote receiver — 2026-11-09

**New receiver source.** The Tab5 can take its IQ from a standard `rtl_tcp` server on the network (for example a Raspberry Pi with an RTL-SDR) instead of a dongle on its own USB port, and can serve its own dongle with an `rtl_tcp` server. Branch `claude/orcsdr-rtl-tcp-interop-f12b5d`; full test record in [docs/RTL_TCP_RELEASE_TEST_PLAN.md](docs/RTL_TCP_RELEASE_TEST_PLAN.md).

- **Client:** standard protocol (12-byte greeting, 5-byte commands, raw 8-bit IQ). Link-adaptive sample rate (1.44, 0.96, 0.24 MS/s ladder; LoRa, POCSAG and P25 stay at 960 kS/s because their decoders need it, and the sample-rate cap does not apply to them), reconnects forever (fast attempts, then every 30 s), names a server whose single client slot is taken ("server busy or silent") and waits up to two minutes for it, and absorbs short DSP stalls instead of dropping samples.
- **Remote capabilities:** the server's greeting (tuner type, gain steps) now drives the gain controls, so manual and auto gain work with no local dongle plugged in. Only what the protocol can carry is offered; the bias tee is never sent to a remote server.
- **Settings > RTL_TCP:** one page for every rtl_tcp setting (source, server, sample-rate cap, frequency correction, direct sampling, offset tuning) with live connection, server and traffic status. The Settings page selector now scrolls.
- **Fixes found while testing:** FT8 and Shortwave SSB read the idle local driver's centre frequency on a remote stream (FT8 decoded nothing, SSB could be up to 6 kHz off); one sample gap made FT8 skip every later slot; an internal RAM leak on each FT8 visit. A band change on a remote stream mis-tuned the FT8 and SSB passband by up to 5 kHz (FT8 decoded nothing after any band button); switching back to AUTO gain could leave the server at an old manual gain. A Wi-Fi outage longer than about four minutes used to leave the Tab5 offline for good (reconnect and C6 recovery are bounded), and a Wi-Fi scan could fail with "radio pause failed" when a remote stream had nothing to start; both are fixed. A server name that does not resolve is reported as "name not found", and a burst of retunes can no longer lose the last one. After a Wi-Fi recovery the remote stream stayed idle (the resume request was dropped); it now restarts. A repeatable outage test (`tools/rtl_tcp_wifi_outage_test.py`, firmware command `RTL_WIFI_OUTAGE`) needs no router. A new Settings > RTL_TCP toggle, KEEP WI-FI ALIVE (on by default, acts only while a remote receiver is selected), re-arms Wi-Fi reconnect and C6 link recovery every 5 minutes while Wi-Fi stays down (docs/RTL_TCP_WIFI_RECOVERY.md); a once-a-minute `RTL_WIFI_HEALTH` log line records the Wi-Fi state. Remote ADS-B names its bandwidth need (about 4.1 MB/s) on screen. The `rtl_tcp` server could be asked to bind `0.0.0.0` (all interfaces) if Wi-Fi reported no address yet; it now refuses any all-interfaces spelling. A new stream resets frequency correction, offset tuning and direct sampling, which a stock server keeps from the previous client.
- **Wi-Fi throughput (Tab5 build):** lwIP pinned to core 1, 256 KB TCP window with scaling, larger lwIP mailboxes, SDIO queues TX 16 / RX 8 with 4 receive staging slots, SDIO clock 20 MHz. Raw TCP receive rose from about 1.8 MB/s to about 3.5 MB/s (28 Mbit/s sustained, `RTL_TCP BENCH`). The larger SDIO transmit queue costs internal DMA RAM (low-water about 20 KB measured). A matching ESP32-C6 Wi-Fi profile (`orcdial/c6/sdkconfig.defaults`: more Wi-Fi buffers, A-MPDU windows 32) takes effect only after the C6 is updated (`RTL_ORCDIAL_C6_UPDATE CONFIRM`); an app flash alone does not apply it. ESP-Hosted 3.1.0 patch variants are included for a future upgrade; the pinned component is still 3.0.6.
- **Over the internet:** the stream was measured across a real cellular hop and decodes (synthetic SSTV frame, 98-101% link). A receive-queue size that was a third of what the TCP window needs made the Tab5 lose data when a server sent large bursts (the stock rtl_tcp sends 256 KB at a time); it is now sized to the window (256 KB bursts: 76% -> 99%).
- **Known gain on connect:** rtl_tcp keeps its tuner gain between clients, so a new stream used to inherit whatever the last client or dashboard left (FT8 about 28 dB too low while showing AUTO). Every remote stream for a new band now starts from tuner AGC.
- **Serial control:** every `RTL_TCP` / `RTL_TCPD` command except `STATUS` needs an authenticated session (extra spaces no longer slip `RTL_TCPD ENABLE` past the check).
- **Server:** off by default, binds only to a named LAN address, never all interfaces. `rtl_tcp` has no authentication; do not expose port 1234 to the internet.
- **Verified on the Tab5 against a Pi 4B running stock RTL-SDR Blog `rtl_tcp` 1.4.0:** all 19 dashboards open on the remote stream; one-hour soak; FT8 decodes (7-16 messages per slot) with and without a local dongle; remote gain changes the measured input level; recovery after a 2-minute server outage. **Not yet verified:** ACARS, SSTV and ADS-B decoding on a remote stream, frequency correction, direct sampling and offset tuning on a real signal, the gain popups on every dashboard, and audio judged by ear, and a server on a public address with no relay. The stock `rtl_tcp` on a Pi was also measured across a real cellular hop through an SSH relay: live FT8, 194 decodes in 31 slots over 8 minutes at 100% link. Changing the receiver source applies at the next stream start, not immediately.

## Unreleased — esp-rtl-sdr driver v0.9.6 — 2026-11-09

**Firmware dependency update.** The Tab5 build now pins `esp_rtl_sdr` to the published release [`v0.9.6`](https://github.com/hardcoreerik/esp-rtl-sdr/blob/v0.9.6/docs/releases/v0.9.6.md) at immutable commit `dabdda1aff6aa196f3c1523d791adf80d2c24ccc` (previously `v0.9.5`, `4bf0171`), in both `idf_component.yml` and `dependencies.lock`.

- **Driver fix carried in:** a dongle that disconnects while the receiver is starting no longer leaves the driver in FAULT or interferes with a newly attached receiver. Disconnect cleanup waits for the start attempt to unwind, and the canceled start returns `NO_DEVICE`.
- **No public API change.** The header changes only its version macro to 0.9.6. OrcSDR's existing start-failure path already handles any non-`ESP_OK` start result, so no caller change is needed.
- **Separate from the SDK USB recovery patch.** The ESP-IDF USB root-port recovery patch from PR #239 is still applied by OrcSDR's build; driver v0.9.6 does not include it.

## Unreleased source/documentation reconciliation — 2026-11-04

**Documentation update, not a new firmware release.** The current-source documentation was reconciled with the native build, implementation, dated hardware records and specific firsthand maintainer acceptance:

- **Native decoder inventory:** VHF FM RDS, ADS-B/Mode S, P25 Phase I and partial Phase II signaling, LoRa/Meshtastic, POCSAG, FT8, FT4, JS8 Normal backend, 14-mode SSTV, ACARS and VDL2; WWV timing-tick detector listed separately. No claim of identical live-RF acceptance across modes.
- **Shortwave and Airband:** working AM/USB/LSB HF reception and intelligible aviation AM voice were confirmed by the maintainer; calibration/band-wide quality remains a different scope.
- **LoRa / Meshtastic:** live unencrypted messages, telemetry, node identity and GPS decode. Encrypted traffic is labeled `ENCRYPTED FRAME`, not decrypted.
- **SSTV:** all supported 14 modes operational per maintainer; recorded conducted four-dongle image/SD tests specifically cover Robot 36, PD120, Scottie S1.
- **ACARS / VDL2:** live 8DPSK/AVLC decoding and aircraft message interpretation, including documented 213 verified frames and 135 ACARS messages in a capture; some airline-specific formats remain unparsed.
- **Receiver coverage:** Blog V4, V3c, V4L and Nooelec NESDR SMArt V5 fully supported. E4000/XTR driver is implemented and works within its native RF range in OrcSDR; the application correctly rejects requests to tune outside the supported band.
- **Dead Air / Unit ENV III:** real receiver measurements and Tab5 sensor temperature/humidity/pressure. VEIL-26 produces reproducible **symbolic interpretations** of real measured values, not scientifically validated paranormal measurements.
- **Roadmap and phase ledger:** remaining work is no longer misrepresented as unimplemented core features. Historical milestones remain date-scoped.
- **Documentation terminology:** implementation, live acceptance, repeatable RF/calibration tests and release-image acceptance are independent claims.

**No firmware code change or new physical device test is implied by this documentation entry.** A feature integrated into current source is not guaranteed to be present in an older published installer image.

## v0.5.0-beta.1 — published 2026-10-26

[Full release notes](docs/releases/v0.5.0-beta.1.md) · [GitHub release](https://github.com/hardcoreerik/OrcSDR/releases/tag/v0.5.0-beta.1)

Key themes: guided first-time setup, OrcMaps and downloadable offline maps, native SSTV, live ACARS, Dead Air, improved Shortwave USB/LSB, LoRa memory improvements and FT8 time/clock improvements. Exact release package and RF-acceptance boundaries are preserved in those release notes; later source or maintainer confirmation does not retroactively change the October 26 build.

## Earlier releases and component-level histories

- [All published GitHub releases](https://github.com/hardcoreerik/OrcSDR/releases)
- [ACARS / VDL2 development log](docs/acars/CHANGELOG.md)
- [FT8 / FT4 / JS8 development log](docs/ft8/CHANGELOG.md)
- [Shortwave Explorer development log](docs/shortwave/CHANGELOG.md)
- [Dead Air and VEIL-26 development log](docs/dead-air/CHANGELOG.md)
- [Dated hardware and release evidence](docs/evidence/)

Do not treat an `Unreleased` heading in a feature-specific development log as a promise that the source is absent from `main`, or a dated "not yet tested" note as a current product availability statement. Read each entry in the context of its commit, branch, and hardware evidence.
