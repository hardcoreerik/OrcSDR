# LilyGO T-Display P4 SX1262: OrcSDR TX Lab

**Branch:** `claude/lilygo-tdisplay-p4-amoled`
**Status:** UI and admission-policy scaffold committed; **no operational SX1262 driver, receive validation, TX, or LilyGO AMOLED rendering claimed**.
**Scope:** T-Display-P4 V1.0 **AMOLED + SX1262** variant. Do not use these assumptions for the TFT or LR2021 assembly.

## Why this exists

OrcSDR currently has an RTL-SDR-driven passive LoRa/Meshtastic dashboard.
The LilyGO exposes an additional independent **hardware packet radio**, the SX1262, suitable for future LoRa/GFSK receive and carefully controlled TX. These are separate sources and measurements:
- **RTL-SDR:** raw CU8 IQ, spectrum/waterfall, and the existing software LoRa PHY/packet decoder.
- **SX1262:** packet-modem operations with its own receive RSSI/SNR and packet CRC; it does **not** output arbitrary raw I/Q.
- A profile in a board catalog does not prove the radio is present, initialized, receiving, or safe to transmit.

The shared LoRa UI remains **five tabs** on RX-only boards. A sixth **TX LAB** tab is included when the board profile declares an SX1262. TX LAB explicitly shows `PROFILE ONLY` and its ARM/SEND affordances are locked. This is a hardware bring-up workspace, **not** a claim of transmission support. The main radio service does not alter its receive behavior.

## Existing facts and source provenance

Source: `components/orcsdr_board_hosyond/src/boards/board_lilygo_t_display_p4_amoled.c`, documented from LilyGO's pinned `lilygo_device_driver` board header (`a47cf73e`); neither wiring nor RF switching is yet exercised in OrcSDR on the owner's unit.

| Interface | Documented connections |
|---|---|
| SX1262 SPI | SCLK GPIO2, MOSI GPIO3, MISO GPIO4, CS GPIO24, BUSY GPIO6, 10 MHz |
| SX1262 RESET | XL9535 expander 0, IO16 |
| SX1262 DIO1 | XL9535 expander 0, IO17 |
| SKY13453 antenna VCTL | XL9535 expander 0, IO1 |
| Expander bus | I2C1 SDA GPIO7 / SCL GPIO8, XL9535 at 0x20 |
| AMOLED / touch | RM69A10 / GT9895, 568 x 1232, **backend not implemented in this console** |

LilyGO's latest public T-Display-P4 README describes **VCTL high = internal antenna** and **VCTL low = external antenna via MMCX1**. The current board profile stores the expander *line*, but does not encode its polarity, and OrcSDR must **not** drive it until the intended antenna path and actual board revision are checked on the bench. MMCX2 is **not** a substitute for MMCX1 on the SX1262 assembly.

Sources:
- https://github.com/Xinyuan-LilyGO/T-Display-P4
- https://github.com/Xinyuan-LilyGO/lilygo_device_driver
- https://github.com/Xinyuan-LilyGO/lilygo_device_driver_example/tree/main/main/examples/lora_tx_rx
- https://github.com/Lora-net/sx126x_driver

Review the upstream driver license before importing code, including commercial redistribution considerations. Prefer an independently testable ESP-IDF SX1262 adapter with explicit board I/O and RF-path ownership. Do not transplant Heltec V4.3 front-end pin and PA settings: they describe a different board.

## Current files

- `apps/orcsdr-tab5/ui/lora_tx_policy.hpp`: platform-neutral, fail-closed TX gate. Uninitialized values deny TX. Readiness levels: absent, profile-only, detected, RX-verified, conducted-TX-verified. Explicit antenna-path, regulatory profile, TX frequency, operator arming, and payload-size gates.
- `apps/orcsdr-tab5/ui/lora_dashboard.{hpp,cpp}`: capability-driven sixth TX Lab tab, real status labels, locked buttons; standard five-tab receive UI remains.
- `apps/orcsdr-p4-hosyond-console/main/screen_lora.cpp`: recognizes the selected LilyGO SX1262 *profile only*, does not probe/initialize radio, and rejects all TX actions.
- `tests/lora_tx_policy_tests.cpp`: host regression covering fail-closed defaults, each denial condition, arming, payload lengths and busy-state denial.

**Important:** `Readiness::tx_verified` is a future driver-provided gate, not a user setting. Do not add a UI shortcut or persistent setting that can set it. The policy is only one layer: the future driver must independently enforce output shutdown, TX bounds, frequency/power, antenna suitability, and fault response. A screen or remote client must never bypass the device-side gate.

## Hardware acceptance plan

1. **Identity and electrical preflight (no radio TX):** Confirm owner PCB, display type, radio module marking, P4 silicon revision and bootloader configuration. Read vendor board header and schematic. Check I2C bus and XL9535 identity/address, default output states, and RESET/DIO1/SKY13453 line mapping. Do not casually change a shared expander bit.
2. **Display gate:** The console currently drives only the Hosyond TC358762 panel. Implement and validate RM69A10 + GT9895 backend (correct 568 x 1232 layout / touch transform) separately. A console that logs GT9895 but remains headless is not a completed LilyGO UI.
3. **Non-radiating SX1262 probe:** Add a board-specific SPI component, with BUSY timeouts and controlled reset via XL9535. Verify chip status and errors readback with actual hardware, cold and warm boots. RX/TX remain disabled during this stage. Do not label `detected` until the *chip* answers a validated command.
4. **Passive packet RX:** Bring up TCXO, modulation profile and IRQ without radiating. Verify packet CRC, RSSI, SNR and timestamp against a known, independent LoRa source. Distinguish hardware SX1262 packet RX from RTL-IQ native decoding, preserve both. No Meshtastic compatibility claim until its exact wire protocol is validated.
5. **Antenna switching:** Confirm manufacturer's VCTL behavior on the *actual* SX1262 SKU, with appropriate external antenna or conducted RF setup. Record signal path, insertion loss, PA limits, TCXO and frontend conditions.
6. **Controlled conducted TX only:** Independently authorize the named device and installed bench chain. Use verified attenuation and input power limits, a single known packet, a short timeout, minimum justified power, no periodic TX, and an independent receive reference. Gate regional frequency and modulation against the applicable authorization and regulation; a regional RX channel picker is **not** TX approval.
7. **TX state machine:** Only after hardware evidence, implement explicit operator arm/disarm, idle-timeout, single-shot send, IRQ completion, fault/timeout hard stop, and an append-only diagnostic event for attempted/denied/started/completed/failed TX. Require a new arm after reboot and network reconnection.
8. **Unified OrcSDR services:** Expose the same truthful capabilities to local screen, browser and optional companion radio. Preserve serial/Web protection: discovery never grants TX permission. Include source/backend identity on every packet event.
9. **Regression acceptance:** Verify Tab5 continues showing exactly five tabs and never exposes TX controls; non-LilyGO P4 console builds remain RX-only; a LilyGO with unverified SX1262 shows locked TX Lab. Test missing/unresponsive SPI, BUSY stuck high, IRQ timeouts, absent/incorrect antenna path, disconnection, unsafe power request, and mixed RTL-SDR + SX1262 operations.

## How to run/test (no device flash or RF emission)

From OrcSDR repository root with a C++17 host compiler:

```bash
g++ -std=c++17 -Wall -Wextra -Werror \
  -Iapps/orcsdr-tab5/ui tests/lora_tx_policy_tests.cpp \
  -o /tmp/orcsdr-lora-tx-policy
/tmp/orcsdr-lora-tx-policy
```

On Windows with a working `g++` on PATH, replace the `/tmp` path with `lora-tx-policy.exe` and execute the resulting file. This test requires no radio, never transmits, and exits zero on pass.

For **build-only** validation of the LilyGO profile, follow `docs/hardware/P4_BOARD_PROFILES.md` and the existing ESP-IDF 5.5 project requirements, using `sdkconfig.board-lilygo-tdisplay-p4-amoled` in an independent build directory. **Do not flash the LilyGO with the Hosyond-only console image.** An IDF compile passes the syntax/links gate only; it is not physical SX1262, display or TX acceptance.

Save the ESP-IDF build log and host test output with the build SHA, exact board SKU and toolchain versions. Preserve `main`; the work in this document is scoped to the existing LilyGO development branch.

## Known gaps and next code deliverable

**The very next implementation should be a non-radiating, timed SPI + XL9535 SX1262 identification adapter with a host/mock test.** This should publish `profile_only` until a real probe succeeds, then `detected`; it must not set `rx_verified` or `tx_verified` based on a simulated response. After that: receive events, stateful TX gate, portrait display/backend and browser parity. No automatic transmit on power-up, dashboard entry, scanning, pairing or hotplug.
