# Profile-driven SX126x transport

The component uses Semtech's unmodified SX126x core driver, tag `v2.5.0`, commit
`a10c5dfdf89788c6ac805e9fe98889de44175aa2`, from
https://github.com/Lora-net/sx126x_driver . The five source/header files in
`vendor/` are byte-identical upstream copies. `vendor/LICENSE.txt` contains the
required Clear BSD notice; retain it in binary distribution notices as well.

The transport and board integration are OrcSDR code. Board pins, SPI host and
clock come from the profile. Only documented `SX1262` profiles are accepted;
LR2021 is a different driver. The SPI bus must be acquired exclusively: an
already-owned bus is rejected before touching chip-select or reset. Transfers
are bounded to 272 bytes, BUSY has a 200 ms deadline, and reset/DIO1 operations
use the existing board expander owner. No full-frame or PSRAM buffer is added.

`orcsdr_sx126x_probe()` runs outside the UI task. It resets the chip, requests
RC standby, reads status, switches packet type to LoRa and back to GFSK with
readback, and reads device errors. This rejects a floating SPI bus. It releases
the SPI device/bus afterward; failure also asserts radio reset where possible.
It does not identify an SX1261 versus SX1262 die, establish radio calibration,
select an antenna, enable the TCXO, start RX, or transmit. A successful result
establishes an SX126x-compatible command response on the documented wiring.
The fitted SX1262 SKU is separately confirmed by the owner.

The authenticated local `RTL_PACKET PROBE` command reports detection and raw
status. It refuses operation during C6 recovery/update. RX/TX qualification
remains false, and TX Lab admission remains locked. Physical setup for the
next paired test is the owner's 906.875 MHz choice, suitable antennas on both
boards, and the Heltec V4 on COM24 running Meshtastic 2.8.0/US LongFast at 5 dBm.
That configuration was read without a firmware/configuration write or test TX.

Host checks (from repository root under WSL):

- `bash tools/test-p4-board-radio.sh`: actual board-control source with an
  I2C register fake; reset/input bit mapping, unrelated output preservation,
  unsupported profiles/lines and timeout/error paths.
- `bash tools/test-p4-sx126x-probe.sh`: actual Semtech driver and OrcSDR HAL with
  SPI/GPIO fakes; command framing, no RF-start opcode, floating bus, lost writes,
  BUSY timeout, SPI errors, occupied-bus preservation, cleanup and retry.

These tests do not prove electrical timing, TCXO voltage/routing, antenna path,
RF reception/transmission, current consumption or physical coexistence.

## Device checkpoint — 2026-11-03

ESP-IDF 5.5.4 build, COM32 app flash and hash verification passed. The unauthenticated
probe was refused; three authenticated probes each reported `detected=1`, standby
mode `2`, command status `1`, and raw device errors `0x0020` (XOSC_START).
The error is retained and reported, not cleared to manufacture a healthy result.
TCXO initialization and calibration are therefore required before RF qualification.
The probe deliberately does not configure the TCXO or start RF.

RTL-SDR remained streaming at roughly 2.4 MS/s at 96.1 MHz before/after the probes;
the UI remained on Home and fresh battery samples continued. These short software
observations do not prove physical audio or sustained coexistence. Wi-Fi failed
its initial saved-network join on this boot, then the existing retry joined
successfully; startup reliability remains an open requirement.

Local evidence: `.local/sx126x-probe-final-build.log`,
`.local/sx126x-probe-flash.log`, `.local/sx126x-device-probe.log`.
At that probe checkpoint no test transmission had been sent from either board.
No C6 image was changed.

## Heltec transmission demonstration — 2026-11-03

With both antennas confirmed by the owner, the Heltec V4 on COM24 submitted one
Meshtastic text packet at its unchanged US LongFast settings: 906.875 MHz from
the default frequency-slot configuration, 5 dBm, hop limit 0, no requested ACK
or response. Packet ID was `2497834197`; its queue result was zero. Text:
`OrcSDR LilyGO P4 Mesh/LoRa test 001 - Heltec V4 at 906.875 MHz.`

The LilyGO's **RTL-SDR**, not its onboard SX1262, was tuned through the Home
owner queue to 906875000 Hz. Before/during/after native PNGs show a strong
time-correlated RF burst and retained waterfall trace; the baseline lacked
that burst. This demonstrates the transmitted burst being observed over RF,
not a CRC-verified LoRa packet or a decoded Meshtastic message. The strong
nearby signal reached about -1 dBFS in the capture, so a later decode test must
check receiver headroom. The onboard SX1262 remains TX-locked and unqualified.

Local evidence: `.local/mesh-transmission.log` and private output captures
`r19-mesh-before.png`, `r19-mesh-transmission.png`, `r19-mesh-after.png`.
The device is left on Home at the requested 906.875 MHz for observation.

## Clock configuration research for the next RF initialization

The current official LilyGO V1 driver initializes its SX126x object without a
TCXO override. Its declared dependency is `llgok/cpp_bus_driver` 2.3.0. The
dependency's `HardwareConfig` defaults to DIO3 TCXO enabled, **1600 mV**, a
**5000 us** startup allowance, and DIO2 RF-switch control. These are configuration
facts, not copied driver implementation. Pins and the SKY13453 antenna selector
remain separate board facts. Exact sources examined:

- LilyGO device driver commit `be01e7d1c3f8b2669f4be88c806f08044cd3af76`,
  [manifest](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/be01e7d1c3f8b2669f4be88c806f08044cd3af76/idf_component.yml)
  and [V1 driver](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/be01e7d1c3f8b2669f4be88c806f08044cd3af76/src/device/t_display_p4/v1/driver.cpp).
- Dependency commit `906f99a10887e81c60076524ed44aa4defcc7642`,
  [SX126x configuration header](https://github.com/llgok/cpp_bus_driver/blob/906f99a10887e81c60076524ed44aa4defcc7642/src/chip/spi/sx126x.h).

A community comment mentioning 3.3 V does not establish this module's TCXO
voltage. Do not substitute 3.3 V for the official driver's 1.6 V setting.
The existing LakeShark source also configures DIO3 before calibration, but its
comment says 1.6 V while the literal voltage byte is `0x01` (Semtech encodes
that as 1.7 V); that mismatch is not a basis for OrcSDR's voltage selection.
No LakeShark or GPL board-driver implementation was copied into this component.
## Clock qualification and serial reply correction — 2026-11-03

The LilyGO profile now explicitly records 1600 mV and 5000 us. The authenticated
`RTL_PACKET CLOCK_CHECK` command reuses the transport probe, programs those
profile facts, clears the old latched errors, performs Semtech's full calibration,
and requires both XOSC standby and no new device errors. It restores RC standby
and releases SPI afterward. The result retains `initial_errors` separately from
post-calibration `device_errors`; a completed clock check is not live RF readiness.
Unknown voltage/timing settings are refused before taking the bus. No RF-start
opcode or antenna selection is added by this check.

Host `bash tools/test-p4-sx126x-probe.sh` passed with cases for an oscillator
failure, XOSC mode failure without error flags, and error flags despite XOSC mode.
All leave clock readiness false and release owned resources. This test checks
the actual driver/HAL commands, including voltage byte `0x00` and timeout
`0x000140` (320 RTC steps), against simulated devices.

ESP-IDF 5.5.4 build and COM32 app flash/hash verification passed. Three consecutive
hardware checks reported initial errors `0x0020`, XOSC mode `3`, post-calibration
errors `0x0000`, `clock_ready=1`, and `ESP_OK`. This verifies operation at the
programmed setting; it is not a physical voltage measurement or startup-time
characterization. One transport-only probe failed readback during an earlier
run; later probes passed. That intermittent event remains a reliability item,
and failed probes continue to deny readiness rather than silently retrying.

Testing also reproduced a malformed serial authentication reply: 120 characters
after `AUTH_OK` instead of 64. The firmware had emitted the proof through
separate printf calls, allowing background logs to split it. Encoding locally
and emitting one printf preserves the existing HMAC protocol. After that change,
20 consecutive real authentication round trips passed device-proof validation;
no credentials, nonces or proofs were logged. This is a bounded test, not a
claim that every serial response format has been audited.

Evidence: `.local/sx126x-clock-auth-build.log`,
`.local/sx126x-clock-auth-flash.log`, `.local/sx126x-clock-auth-device.log`,
`.local/serial-auth-device.log`. RTL remained streaming at about 2.4 MS/s;
fresh gauge readings continued. Wi-Fi's initial join failed again on this boot,
so startup reliability remains open. Reception, antenna-path qualification,
transmission and sustained coexistence remain open; TX Lab stays locked.
