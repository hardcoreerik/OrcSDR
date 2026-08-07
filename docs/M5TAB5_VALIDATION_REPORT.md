# M5Tab5 validation report

Date: 2026-07-18

Target: M5Stack Tab5 on COM17

Status: native display/touch, authenticated direct-USB, bounded edge resilience, read-only Wi-Fi inventory, and read-only battery telemetry verified; network association/TLS and production hardening remain open

## Repo-verified

- The firmware builds with ESP-IDF 5.5.3 for `esp32p4` and pre-v3 silicon,
  minimum revision v0.1. That silicon family selection matches the official
  M5Stack Tab5 demo configuration.
- The original ESP-IDF serial image and the native pioarduino/M5Unified UI both
  build for the measured pre-v3 ESP32-P4 silicon. Firmware 0.6.1 uses 1,030,986
  bytes of its 6.25 MiB app partition and 36,428 bytes of internal RAM. Its
  1,075,200-byte `firmware.bin` SHA-256 is
  `1840080439787255D2592FA78EB6673057AABBB1D8EE86C9F0526CF5E641881D`.
- The firmware installs an interrupt-driven USB Serial/JTAG console and emits
  protocol-v1 `hello`, `node_snapshot`, and sequenced `heartbeat` envelopes.
- The host adapter ignores non-JSON boot diagnostics, strictly deserializes
  adapter envelopes, rejects unsupported protocol majors, and defaults to
  COM17.
- The UI advertises device info, health, display status, touch input, journal
  synchronization, read-only network inventory, and authenticated network
  configuration. Hardware-output capabilities remain absent.
- The `m5tab5.power.read` snapshot preserves raw M5Unified values and explicit
  support metadata. It does not infer charger state when no PMIC is detected.
- The host adapter persists a 32-byte credential outside the repository,
  provisions it only while the device is unpaired over direct USB, verifies a
  mutually authenticated random challenge with domain-separated HMAC-SHA256,
  and forwards no device JSON before authentication succeeds.
- The host adapter accepts Wi-Fi credentials only from a named JSON file,
  validates SSID/password byte bounds, hex-encodes them for serial framing, and
  signs the configuration with the paired key. Firmware verifies that HMAC
  before writing NVS and never emits either secret.
- The firmware keeps an eight-entry NVS ring journal. The host acknowledges
  stable journal sequence numbers after forwarding them, reports overwritten
  records as pressure, and clears the pressure counter only after host acknowledgement.
- The only installed offline workflow is the built-in `status_on_disconnect`.
  Its per-device-HMAC-signed configuration has a monotonic revision and a
  persistent maximum run count; there is no general workflow language on-device.

## Runtime-observed

- USB identity: Espressif `VID_303A&PID_1001`, USB Serial/JTAG, COM17.
- Chip: ESP32-P4 revision v1.3, dual HP cores plus LP core, 40 MHz crystal.
- Flash: 16 MiB. eFuse capability data and vendor documentation identify 32 MiB
  octal PSRAM.
- Security state: secure boot disabled, flash encryption disabled, JTAG enabled,
  and ROM download modes enabled. This unit is a development target, not a
  production trust anchor.
- The complete 16 MiB factory image was backed up before writing. Local recovery
  artifact: `.device-backups/tab5-30eda0e2e695-factory-20260718.bin`; SHA-256
  `9A57B3947FF63273435F34045730FEA58B76F01C1920453AC1EBB7FBE841CB8A`.
- The original image reported ESP-IDF 5.5.4 and used dual 6.25 MiB app slots,
  SPIFFS, NVS, OTA metadata, and a coredump partition.
- The compatible OrcLink bootloader, partition table, and native UI application
  were written to COM17. Esptool verified every written region by hash.
- The application emitted authenticated `hello`, `node_snapshot`, journal, and
  heartbeat envelopes under stable identity `m5tab5_30eda0e2e695`.
- A deliberately invalid host proof returned `AUTH_DENIED`; a subsequent
  unauthenticated `PING` produced zero JSON envelopes.
- Runtime snapshot: 1280x720 display, `touch_ready=true`, 16 MiB flash, 32 MiB
  PSRAM, and 422,680 bytes free heap after authentication.
- ESP-Hosted initialized the ESP32-C6 over the measured SDIO pin map and
  reported host firmware 2.12.3 and slave firmware 2.12.6. Firmware 0.4.1
  completed its asynchronous station-mode scan without blocking USB auth;
  two authenticated snapshots eight seconds apart reported
  `station_ready=true`, `scan_result=10`, `connected=false`, and about 337.8 KiB
  free heap. SSIDs were discarded and never emitted.
- After the guarded 0.5.2 update, an unprovisioned authenticated snapshot
  advertised `m5tab5.network.configure` and reported `configured=false`,
  `connected=false`, `scan_result=16`, `touch_ready=true`, and 337,560 free
  heap bytes. This verifies the
  no-credential path and state reporting; association remains untested.
- Two 0.6.x snapshots sampled battery level 45-46%, voltage 7,332-7,338 mV,
  and current from -187 to -195 mA. M5Unified returned PMIC type 0 (unknown)
  and VBUS -1 (unsupported), so firmware 0.6.1 reports
  `pmic_detected=false` and `charging=unknown`. This proves battery telemetry,
  not charging/USB-source detection or calibrated runtime estimates.
- Physical touches produced mapped coordinates `(773,370)`, `(802,342)`,
  `(758,323)`, and `(724,358)` and were journaled with stable sequences 4-7.
- After the host adapter was stopped for longer than the five-second lease, the
  device retained journal record 8 (`session_degraded`). Reusing the same
  credential replayed record 8 followed by record 9 (`session_online`), and
  acknowledgements reduced pending records to zero.
- An app-only rewrite/reboot preserved both NVS identity and journal state.
  Reauthentication continued at record 10 (`boot`) and record 11
  (`session_online`), then cleared the pending queue.
- Revision 1 of `status_on_disconnect` was installed with a three-run bound.
  Three host-loss transitions produced durable `workflow_run` records and the
  persistent run count stopped at 3/3.
- A forced ten-record pressure test observed nine overwritten records after
  existing pending traffic. Reconnect replayed the surviving stable sequences;
  pressure acknowledgement produced `dropped: 0` and journal acknowledgement
  produced `journal_pending: 0`.
- Credential rotation replaced the host key file through a same-directory
  temporary file, persisted the new NVS credential, journaled
  `credential_rotated`, and reauthenticated. The prior credential then returned
  `PAIR_LOCKED`; its temporary proof copy was deleted.
- Built-in JTAG enumerates and can halt both HP cores. Some USB/JTAG reset-line
  combinations enter ROM download mode (`boot 0x204`) and leave the display
  blank. A watchdog reset released that state, and explicitly opening the host
  adapter with DTR deasserted prevented recurrence. The guarded 0.3.0 flash then
  completed with audit `.orclink/firmware-test-1784420664.jsonl`.
- The guarded 0.4.1 app-only update passed artifact, USB identity, target MAC,
  flash, clean reboot, stable node ID, and exact-version verification. Audit:
  `.orclink/firmware-test-1784435099.jsonl`.
- The guarded 0.5.2 app-only update passed the same gates with bounded retries
  for transient USB/journal-settlement races and retained panic/watchdog
  rejection. Audit: `.orclink/firmware-test-1784435904.jsonl`.
- The guarded 0.6.1 update passed exact artifact, USB identity, target MAC,
  flash, reboot, version, and node checks. Audit:
  `.orclink/firmware-test-1784436265.jsonl`.
- Firmware 0.7.2 replaced Arduino HWCDC with ESP-IDF's USB Serial/JTAG driver
  so COM17 can coexist with the ESP32-P4 High-Speed host controller. The
  guarded update passed, and the attached RTL-SDR.COM V4 reported
  `0bda:2838`, High-Speed, serial `00000001`. Audit:
  `.orclink/firmware-test-1784437974.jsonl`.

## Authoritative platform facts

M5Stack documents the Tab5 with an ESP32-P4 main controller, ESP32-C6-MINI-1U
wireless coprocessor, 16 MiB flash, 32 MiB octal PSRAM, and a 5-inch 1280x720
touch display. The board also exposes USB host/OTG, microSD, RS485, audio,
camera, IMU, RTC, and battery interfaces. See the
[Tab5 product documentation](https://docs.m5stack.com/en/core/Tab5) and the
[official user demo](https://github.com/m5stack/M5Tab5-UserDemo) pinned during
validation at commit `68b19d37fbf9cefd5f256992f5dca34794c62ab4`.

## Gate result

The exact board, recovery path, stable identity, native touch UI, one telemetry
source, physical-USB authenticated session, bounded journal, degraded state,
host-loss replay, and app-reboot persistence are proven on hardware. The native
UI choice is therefore evidence-backed.

Full Phase 2 remains open on four explicit gates:

- Wi-Fi coprocessor initialization and passive inventory are proven. Association
  and TLS/certificate validation still need network credentials and AP/daemon
  loss testing.
- The development unit's NVS key is not protected because secure boot and flash
  encryption are disabled. Burning irreversible eFuses requires a separate
  production-hardening decision and is not part of this proof.
- Bounded ring-pressure signaling and host acknowledgement are measured.
  Sudden-power-loss, wear, and destructive full-storage behavior remain unmeasured.
- Power, thermal, battery, sleep/wake, and vendor recovery measurements remain
  unmeasured beyond the read-only battery snapshot; runtime calibration and
  VBUS/charger-source detection remain open.
