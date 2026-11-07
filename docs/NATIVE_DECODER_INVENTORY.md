# Native signal decoder inventory — OrcSDR ESP32-P4

**Current-source inventory:** reviewed 2026-11-04 from the `apps/orcsdr-tab5/main/CMakeLists.txt` firmware source list, dashboard registry, decoder implementation files and existing hardware/host records. **Current source is not automatically the same as every published release image.** See [PROJECT_STATUS](../PROJECT_STATUS.md), [field acceptance](MAINTAINER_FIELD_ACCEPTANCE_2026-11-04.md) and the [versioned releases](https://github.com/hardcoreerik/OrcSDR/releases).

**Native means the receiving/decoding logic runs in OrcSDR's ESP32-P4 C/C++ firmware**, not in a browser, cloud decoder, tethered Linux/PC application, or standalone SDR server. The independently implemented `esp-rtl-sdr` driver supplies tuner control and IQ; it does not supply these OrcSDR protocol decoders. Implemented does not imply every mode has identical hardware-test coverage.

## Protocol and signal decoders present in the native Tab5 build

| Decoder / mode | Application dashboard | Native implementation | Status and precise limit |
|---|---|---|---|
| **FM RDS** | FM Radio | Broadcast FM audio/RDS receive integration and FM display | Working Program Service (PS), RadioText, PI and PTY display from valid received RDS groups; live station/RDS hardware tests exist. |
| **ADS-B / Mode S** | ADS-B | `ui/adsb_decoder.cpp` | Live 1090 MHz Mode S/ADS-B demodulation, parity/CRC gating, aircraft identity, altitude and CPR position recovery. RF-validated with live aircraft. |
| **P25 Phase I** | P25 Radio | `ui/p25_decoder_core.cpp`, `ui/p25_decoder.cpp`, `ui/p25_voice.cpp`, IMBE support in `components/mbelib` | Native C4FM/CQPSK control/voice follow with supported clear voice and encrypted-call indication/muting. Encrypted voice is not deciphered. |
| **P25 Phase II signaling** | P25 Radio | Phase II burst/sync and DUID recognition in `ui/p25_decoder_core.cpp` | **Partial protocol reception:** grants/synchronization/classification and observation exist. Phase II payload/AMBE+2 voice decoding is **not** implemented. Do not market as full Phase II voice. |
| **LoRa + Meshtastic** | LoRa / Mesh | `ui/lora_native_decoder.cpp`, `ui/lora_preamble_detector.cpp` | Maintainer-confirmed live unencrypted messages, telemetry, node info and GPS; `ENCRYPTED FRAME` detection without falsely claiming plaintext recovery. Receive-only. |
| **POCSAG** | POCSAG | `ui/pocsag_decoder_core.cpp` | Native FSK, 512/1200/2400 baud candidate paths, BCH error correction, CAPCODE/messages; **1200 baud RF-verified**, other baud rates have narrower test coverage. |
| **FT8** | FT8 RX | `ui/ft8_native_backend.cpp`, `ui/ft8_demod.cpp`, `ui/ft8_ldpc_decode.cpp`, `ui/ft8_message.cpp` | Native synchronous weak-signal acquisition, LDPC/CRC and message extraction; accepted live 7.074 MHz reception on V4, V3c, V4L, Nooelec V5. |
| **FT4** | FT8 RX | `ui/ft8_mode.cpp` FT4 profile, `ui/ft8_native_backend.cpp`, shared LDPC/CRC pipeline | Native mode marked `implementation_ready` and compiled into P4 firmware; do **not** infer the FT8 four-dongle on-air test also validates FT4 live reception. |
| **JS8 Normal** | FT8 RX | `ui/js8_native_backend.cpp`, `ui/js8_decoder.cpp`, `ui/js8_fec.cpp` | Native backend compiled and wired to the runtime; C++ decoder recovered real recorded JS8 Normal frames in host tests. Physical user-visible JS8 message acceptance remains narrower; Fast/40/Slow/60 are not qualified as operational modes. |
| **SSTV image modes (14)** | SSTV | `ui/sstv_receiver.cpp`, `ui/sstv_modes.cpp`, `ui/sstv_auto_detect.cpp` | **All 14 modes operational per maintainer.** Robot 36/72; Martin M1/M2; Scottie S1/S2/DX; PD 50/90/120/160/180/240/290. Four-dongle conducted image/SD tests are documented for Robot 36, PD120 and Scottie S1, not every one of the 14. |
| **VHF ACARS** | ACARS | `ui/acars_core.cpp`, `ui/acars_receiver.cpp`, `ui/acars_interpret.cpp` | Native 2400 bps MSK ACARS reception, checksum-valid blocks and interpreted aircraft messages with history/archive; maintainer confirms it works on-air. Unknown airline-specific payloads remain raw. |
| **VDL Mode 2** | ACARS | `ui/vdl2_receiver.cpp`, `ui/vdl2_burst.cpp`, `ui/vdl2_rs.cpp`, `ui/vdl2_frames.cpp` | Native 8DPSK, burst, Reed–Solomon, AVLC/ACARS handling; on-device 136.975 MHz session recorded **213 verified frames and 135 ACARS messages**, including aircraft position and climb-related content. Application payload interpretation remains partial. |

**Additional native signal detector (not a full communications decoder):** `ui/wwv_tick.cpp` implements a WWV 1000 Hz timing-tick detector for FT8 clock measurement/calibration workflows. Synthetic and captured-signal host evidence exists, but an exact flashed-device tick acceptance is separately scoped. This is **not** a full WWV message decoder.

This is an inventory of **12 protocol/mode decoder entries plus one timing detector**. Some entries are related (e.g. ADS-B is part of Mode S; VDL2 transports ACARS; P25 Phase II signaling is not independent complete audio decoding), so the count describes user-visible capabilities, not unrelated patented codecs or a benchmark score.

## Native SDR demodulators and receive workflows (not separate digital message decoders)

- **FM/WFM, stereo FM + RDS integration** — FM Radio.
- **AM envelope and filters** — AM Radio and aviation Airband AM.
- **NFM** — NOAA Weather Radio, shared VHF receiver, and other appropriately configured channels.
- **USB/LSB SSB** — Shortwave Explorer and CB listening with frequency-aware filters/clarifier.
- **Signal energy and activity classification** — RF Lab, RF Visualizer, AM/CB/airband scanners and Dead Air.
- **ENV III temperature/humidity/pressure** — real sensor measurements in Dead Air; not an RF or paranormal decoder.
- **VEIL-26** — deterministic **symbolic interpretation** of real measured RF input; not a recovered datalink message or scientific paranormal detector.
- **Wi-Fi AP scan** — ESP32-C6/Hosted network survey; not RTL-SDR IQ decoding.

## Native code on development branches, not a released/mainline dashboard decoder

- **HFDL:** `chatgpt/hfdl-dashboard-decoder` contains a native receiver frontend and dashboard scaffolding, **not a verified HFDL frame decoder**. The branch explicitly lacks acquisition, 1800-symbol/s PSK/FEC and verified SPDU/link parsing; it is not merged to main.
- **WEFAX:** `chatgpt/wefax-decoder-development` contains a host-tested C++ manual-frame grayscale fax core, schedule catalog and M5GFX UI work. Automatic phasing, live SDR/audio integration and on-device RF acceptance remain unfinished, so it is **not** a working production WEFAX decoder on main.

## Publication rules

1. **Source present and built**, **host decoding proven**, **physical RF reception proven**, and **full per-mode qualification** are separate evidence levels; show the highest supported by records or an explicitly attributed maintainer report.
2. Do not count ordinary frequency tuning, mapping, E4000 driver control, sensor readings, or symbolic numerology as independently decoded RF protocols.
3. Update the README, user-guide status table and this inventory in the same pull request when a decoder becomes available, is newly RF-validated, or changes status.
4. Keep exact historical release notes and older experiment/handoff records as date-scoped evidence rather than rewriting what was known at the time.
