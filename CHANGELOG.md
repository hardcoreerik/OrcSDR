# OrcSDR changelog

This is the **project-level release and current-source index**. It links the authoritative, version-specific release notes and feature-level development records rather than rewriting historic test results. For a current source/receiver matrix see [PROJECT_STATUS.md](PROJECT_STATUS.md), and for on-device decoders see [Native Decoder Inventory](docs/NATIVE_DECODER_INVENTORY.md).

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
