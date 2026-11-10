# OrcSDR changelog

This is the **project-level release and current-source index**. It links the authoritative, version-specific release notes and feature-level development records rather than rewriting historic test results. For a current source/receiver matrix see [PROJECT_STATUS.md](PROJECT_STATUS.md), and for on-device decoders see [Native Decoder Inventory](docs/NATIVE_DECODER_INVENTORY.md).

## Unreleased: PlutoSDR (AD9361) as a USB receiver source for OrcSDR P4 Core, in progress — 2026-11-10

**Not a release.** Work on `claude/pluto-p4-core-source`, branched from `claude/orcsdr-web-landing`; nothing is merged.

- **New source, receive only:** with `sdkconfig.pluto` (`CONFIG_ORCSDR_PLUTO_SOURCE`) a PlutoSDR on the board's USB-A port becomes receiver 0 when no RTL-SDR is streaming there. Its 12-bit IQ is converted to the 8-bit stream every consumer already takes (audio, RF Lab, decoders, browser IQ). No transmit attribute is ever written. The iiod client, IQ conversion and USB transport live in the separate `esp-pluto-sdr` repository (pinned in `main/idf_component.yml`), which does not depend on OrcSDR.
- **Seam:** `radio_service` gained a small `radio::ext` interface (acquire/release, requests, publish, feed) and an idle path for the RTL-SDR loop; the Pluto logic is in `main/pluto_service.cpp`.
- **Device-derived limits:** tune span (70 MHz to 6 GHz on the unit tested), the rate floor and the gain span are read from the Pluto, not assumed. Console rates map to the AD9361 by integer decimation (960 kS/s from 2.88 MS/s, 2.4 MS/s direct).
- **Verified on COM28** (Waveshare ESP32-P4-WIFI6-POE-ETH, rev v3.2, Pluto on the USB-A port): 960 kS/s and 2.4 MS/s delivered at the requested rate (2.398 to 2.404 MS/s steady at 2.4 MS/s), no dropped blocks, one boot. A first version that converted inline with the USB read delivered only about 2.3 MS/s because the Pluto overruns when the host is late; the reader is now a separate higher-priority task.
- **Flash clock:** this v3.2 unit hit "mismatch chip ID" in the second-stage bootloader at an 80 MHz flash clock; `sdkconfig.flash-40m` fixes it.
- **Not verified:** sensitivity or calibration against a known signal, sustained hours, Pluto together with an RTL-SDR, hot-unplug recovery, gain behaviour across bands, a Pluto on the Tab5 or any other board, decimation filter quality (boxcar).

## Unreleased: OrcSDR Web and OrcSDR P4 Core landing branch, in progress — 2026-11-08

**Not a release.** Work on `claude/orcsdr-web-landing`, not on `origin/main`.

- **OrcSDR P4 Core** (`apps/orcsdr-p4-core`): the firmware for ESP32-P4 boards, built from the shared OrcSDR screens, with OrcSDR Web in the image. Built for the Waveshare ESP32-P4-ETH and ESP32-P4-WIFI6-POE-ETH.
- **OrcSDR Web** (`apps/orcsdr-web`): the browser page, the signed-request login and the port-81 IQ and audio sockets. The page now compiles OrcMaps; no map file is supplied to the browser yet, so maps show "NO BASEMAP".
- **Waveshare ESP32-P4-ETH (COM29):** health check and the browser protocol tests pass on Ethernet. **Waveshare ESP32-P4-WIFI6-POE-ETH (COM28):** boots and finds its IP101 PHY and the ESP32-C6; link and web tests not yet run.
- **Browser basemap delivery:** the board serves `/sd/orcmaps/basemap.pmtiles` to the page in signed 32 KiB chunks (`POST /api/v1/map/status` and `/map/read`); the page writes it into the WebAssembly virtual filesystem and rescans, with a status line (no SD card, no file, loading, loaded). Tested on COM28 for the absent path only (`tools/web_maps_test.js`): no board has a card with a basemap, so delivery of real data and the browser rendering of a map are untested.
- **Not verified:** the P4 map service with a real basemap, browser audio heard by a person, any other board.

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
