# ESP32-P4 source conflicts and revision traps

Snapshot 2026-10-28. The following are **open research gates**, not proven physical faults. Source-code-effective constants are not a replacement for schematic nets; samples are not guaranteed compiled. Any proposed bench probe requires separate on-device authorization and electrical safety assessment.

## C01 — Elecrow 7-inch v1.2 SDMMC schematic versus example code

- Evidence: [Elecrow's v1.2 board maintenance guide](https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen/blob/a25ecadb4b4d28e9e904e114a6a1b82ae6f84624/Eagle_SCH&PCB/1.2/readme.md) reports V1.2 schematic onboard SDMMC D3 GPIO14 / D2 15 / D1 16 / D0 17 / CLK18 / CMD19; a sample instead declares CLK43, CMD44, D0 39.
- **Status:** `CONFLICT`, source-vs-schematic, confirmed as a reported discrepancy in an in-repo guide; raw `.sch` and code have not been independently parsed in this web pass. The guide itself states it did not run physical checks.
- Risk: incorrect SD pins can collide with C6 SDIO or fail to mount, and can create bus-drive conflicts.
- Gate: read exact `Eagle_SCH&PCB/1.2/*.sch` netlist and original lesson driver, tag both SHAs; continuity and logic-analyzer test on a *marked V1.2 PCB* before selecting production mapping.

## C02 — Elecrow 7-inch v1.2 radio GPIO code versus Kconfig

- Evidence: [same vendor repository guide](https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen/blob/a25ecadb4b4d28e9e904e114a6a1b82ae6f84624/Eagle_SCH&PCB/1.2/readme.md) reports effective SX1262 IRQ/RESET 27/28 versus configurable defaults 53/54 (also nRF24 CE/CS), and collisions with UART use.
- **Status:** `CONFLICT` in different source layers, not physical board validation.
- Risk: simultaneous peripheral enable or wrong GPIO voltage/edge; RF module may remain held in reset.
- Gate: inspect Kconfig/header active macro expansion, exact v1.2 `.sch` nets, then boot/log and observe GPIO only after circuit/power approval.

## C03 — Elecrow 7-inch C6 SDIO data order across v1.0 / v1.1+

- Evidence: [vendor 7-inch readme revision table](https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen/blob/a25ecadb4b4d28e9e904e114a6a1b82ae6f84624/readme.md) specifies change from C6-side GPIO14/15/16/17 (D0–3) on v1.0 to GPIO17/16/15/14 (D0–3) on v1.1 and v1.2. This is **a documented revision difference**, not necessarily a defect.
- Risk: using a pinmap from the wrong board revision prevents C6 bus enumeration and may affect SD-card sharing.
- Gate: label the actual PCB v1.0/v1.1/v1.2; compare board-specific schematic, C6 coprocessor settings and P4 host pins separately; test SDIO bus after firmware image identity is known.

## C04 — Waveshare USB host jumper reversal, Rev1.1 versus Rev1.2

- Evidence: [official Waveshare FAQ](https://docs.waveshare.com/ESP32-P4-WIFI6-DEV-KIT/FAQ) describes Rev1.1 HOST ports 2–4 / DEVICE port1 versus Rev1.2 HOST port1 / DEVICE ports 2–4.
- **Status:** `VERIFIED_DOCUMENTARY` revision-dependent routing, not electrical runtime measurement.
- Risk: wrong port assumption may connect RTL-SDR to device mode or backfeed a supply when external power is attached.
- Gate: photograph exact PCB silkscreen, locate applicable jumper rail and actual USB VBUS switch, measure voltage/current by separately approved procedure; do not recommend connecting +5V jumper speculatively.

## C05 — Tab5 panel-controller variants, not a conflict to conceal

- Evidence: [Espressif's pinned BSP display driver](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/src/bsp_display.c) recognizes ILI9881C/GT911, ST7123 and ST7121 using touch probing/firmware. These are three distinct supported paths; a prior two-variant assumption would miss ST7121.
- Risk: hardcoding one DSI reset/init table can lead to a blank screen or nonfunctional touch on later assemblies.
- Gate: log physical Tab5 panel identifiers and touch probe, compare init sequence and DSI bit rate under ESP-IDF 5.5, then sign off by actual assembly type.

## C06 — Elecrow USB *device* example versus proposed OrcSDR *host*

- Evidence: [Elecrow 7-inch v1.2 guide](https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen/blob/a25ecadb4b4d28e9e904e114a6a1b82ae6f84624/Eagle_SCH&PCB/1.2/readme.md) identifies native TinyUSB HID **device** code, with no demonstrated USB VBUS sourcing for a receiver.
- Risk: assuming `USB2.0` connector and device demo imply host voltage/current. No host test is present.
- Gate: inspect exact `.sch` for port routing/power path; capture successful USB Host enumeration and sustained RTL-SDR I/Q on the *exact board revision* before giving host feature a positive mark.

## C07 — Espressif Function-EV generic BSP versus PCB revisions

- Evidence: [Function-EV BSP header](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/esp32_p4_function_ev_board/include/bsp/esp32_p4_function_ev_board.h) contains generic I2C/I2S/SD pin constants, not an individual v1.4/v1.5.2 schematic proof.
- Risk: silently treating generic code as v1.5.2 header wiring, or conflating board PCB rev and P4 silicon stepping.
- Gate: obtain each PCB-specific Espressif schematic/user guide rev, diff USB connector routing and header GPIO; separately query chip stepping on the device after hardware-read authorization.

## C08 — LilyGO C6 SDIO demo protocol versus ESP-Hosted-MCU requirement

- Evidence: [LilyGO example manifest](https://github.com/Xinyuan-LilyGO/T-Display-P4/blob/7b021f795f6842fe6d62d3b2f2931cd0f03d3f80/main/idf_component.yml) and example name `esp32c6_at_host_sdio_wifi`, versus [Espressif ESP-Hosted-MCU manifest](https://github.com/espressif/esp-hosted-mcu/blob/78b1c1c43c02b0c4e898cf87f94679278b1ddaff/idf_component.yml) v3.0.10. Both describe a P4/C6 SDIO avenue, but **an AT SDIO example is not evidence of a compatible ESP-Hosted coprocessor image**.
- Risk: matching physical SDIO lines but using incompatible wire protocol/firmware makes OrcSDR network fail.
- Gate: inspect driver dispatch and CP image flashing metadata; explicitly record version/protocol pair, host SDIO lines and observable connection before `VERIFIED_RUNTIME`.

## C09 — M5 Tab5 BSP USB-host function accepts but does not use power mode/current-limit parameters

- Evidence: [Espressif Tab5 `bsp_usb.c`](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/src/bsp_usb.c) `bsp_usb_host_start(mode, limit_500mA)` enables expander USB power and installs the host library, but its implementation does not reference `mode` or `limit_500mA` beyond the signature.
- Risk: an API shape could falsely imply that VBUS power-source selection or a 500-mA limit is enforced.
- Gate: inspect actual board switch silicon and upstream BSP changes; do not infer electrical current limit from function signature, capture VBUS voltage/current under approved RTL-SDR stress test.

## Conflict review states

Every item above remains OPEN; no actual wiring conflict has been cleared by a physical observation in this pass. A report that code and schematic differ is not a claim that the product is dangerous or defective in manufacture. Neither `rsa publish` nor a documentation-only change should close a hardware gate.