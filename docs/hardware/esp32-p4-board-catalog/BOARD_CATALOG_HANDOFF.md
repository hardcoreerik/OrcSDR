# OrcSDR ESP32-P4 manufacturer board catalog — documentary research handoff

**Source snapshot:** 2026-10-28 | **Research branch:** `research/esp32-p4-board-catalog-20261028` @ `3694242a0748c3c89890fb223914db866ff25de0`  
**Execution level:** primary vendor SDK/docs inspected through GitHub connector; **no RSA import, IDF build or physical observation** in this web session.

## Objective and acceptance

A full-console profile requires identified PCB revision + actual panel, DSI controller and touch, powered USB Host port supporting a sustained RTL-SDR V4 I/Q stream, working ESP-Hosted P4↔C6 over correctly mapped SDIO with matching firmware, and clean OrcSDR ESP-IDF 5.5.x runtime logs. A DSI connector without a panel, a device-only port, a powered module without actual C6 firmware, or an SDK example alone must never be called console-ready. A headless radio appliance is a distinct, valid OrcSDR/OrcNode target, not a failed console.

## Priority candidates — **documentary candidates only**

### Priority A — full console candidates

1. **M5Stack Tab5 (all three panel paths).** The Espressif 1.3.2 BSP explicitly supports ILI9881C/GT911, ST7123 and ST7121 detection. Display setup cannot be hard-coded to the first production panel. The BSP declares a separately enabled USB feature on IO-expander #2 bit 3, C6 enable on #2 bit 0, SDMMC GPIO39–44, and panel/touch control on first expander. The [USB host BSP](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/src/bsp_usb.c) installs USB host library, but its `mode` and `limit_500mA` parameters are not used in the inspected function; *this is not proof of 500-mA current limiting*. Confirm actual power circuitry and C6 firmware with a matching revision. [BSP header](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/include/bsp/m5stack_tab5.h) / [display selection](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/src/bsp_display.c).
2. **LilyGO T-Display-P4 TFT or AMOLED v1.0.** Of the inspected manufacturer sources, the [pinned LilyGO board header](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/a47cf73e2c9d442c0f00b618749150f4482dcd0a/src/device/t_display_p4/t_display_p4_config.h) yields especially complete I2C/expander/SPI/SDIO/I2S/GNSS/RMII assignments. The 4.05-inch TFT and 4.1-inch AMOLED have separate panel/touch paths, not interchangeable in code. IDF requirement is `>=5.5.4` per [manifest](https://github.com/Xinyuan-LilyGO/T-Display-P4/blob/7b021f795f6842fe6d62d3b2f2931cd0f03d3f80/main/idf_component.yml). **Its C6 examples are labeled AT-host-over-SDIO; that does not prove ESP-Hosted-MCU protocol compatibility.** USB PHY IO10 is not a confirmed switched 5 V VBUS. Submodule hash: `a47cf73e2c9d442c0f00b618749150f4482dcd0a`.
3. **Elecrow CrowPanel Advanced 7-inch, by exact PCB revision.** Its 7-inch EK79007 1024×600 DSI panel is a possible dashboard target. Revision v1.0 to v1.1 changes the C6-side data line order; v1.2 has [source/schematic SDMMC and RF GPIO conflicts](https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen/blob/a25ecadb4b4d28e9e904e114a6a1b82ae6f84624/Eagle_SCH&PCB/1.2/readme.md). The repository includes a USB **device** HID example, which is not evidence of USB Host/5 V VBUS for an RTL-SDR. Do not classify as verified console.
4. **Seeed reTerminal D1001.** Integrated DSI/touch/C6 class makes it interesting, but USB host and VBUS are unproved from the inspected [official getting-started guide](https://wiki.seeedstudio.com/getting_started_with_reterminal_d1001/).

### Priority B — conditional, expandable, or headless

- **Waveshare P4-WIFI6-DEV-KIT.** Board revision-specific jumpers are critical: [Waveshare FAQ](https://docs.waveshare.com/ESP32-P4-WIFI6-DEV-KIT/FAQ) reverses the host/device selection between Rev1.1 and Rev1.2. An actual installed DSI panel must be independently identified. Host current not measured.
- **Waveshare P4 Module-DEV-KIT.** DSI expansion + USB-A OTG suggests prototyping feasibility. Confirm the precise power-switch circuit and optional C6 wiring before classifying a complete configuration ([vendor wiki](https://docs.waveshare.com/ESP32-P4-Module-DEV-KIT)).
- **DFRobot FireBeetle 2 (DFR1172).** [Official wiki](https://wiki.dfrobot.com/dfr1172/) declares C6 SDIO GPIO14–19, SD GPIO39–44, high-speed USB OTG and DSI connectors (screen is not integrated). Early-lot C6 firmware caution ([manufacturer note](https://wiki.dfrobot.com/dfr1172/docs/22902); exact affected batch/firmware to confirm).
- **M5Stack Stamp-P4 + C6 add-on.** Compact modular base for *headless* OrcSDR/OrcNode; external powered USB host and DSI/touch need a designed carrier. An OTG D+/D- breakout alone is not power delivery ([Stamp-P4](https://docs.m5stack.com/en/core/Stamp-P4), [C6 add-on](https://docs.m5stack.com/en/stamp/Stamp-AddOn_C6_For_P4)).
- **M5Stack PoE-P4 Unit.** Dedicated host connector, Ethernet PHY and optional DSI make this a promising PoE headless prototype. Measure USB host 5V power/current and check co-processor/transport design independently ([official unit](https://docs.m5stack.com/en/unit/Unit_PoE-P4)).
- **Olimex ESP32-P4-DevKit Rev D1.** Strong electrical-design reference, *not an integrated console*. Existing RSA/REA board study is in [REA-Results](https://github.com/hardcoreerik/REA-Results/blob/main/targets/esp32-p4-devkit/notes/pcb-lessons-2026-10-16.md); reuse before redoing PCB metrics. No Wi-Fi/C6 integrated in the previously analyzed board.
- **Espressif Function-EV v1.4/v1.5.2.** The [official BSP](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/esp32_p4_function_ev_board/include/bsp/esp32_p4_function_ev_board.h) declares I2C, I2S, SD and screen variants, but its generic header cannot be silently promoted into revision-specific wiring. Distinguish v1.4/v1.5.2 port routing and P4 silicon stepping.

### Priority C — alternate display backend or discovery

- **Elecrow 5-inch v1.0:** vendor [RGB GPIO list](https://github.com/Elecrow-RD/-CrowPanel-Advanced-5inch-ESP32-P4-HMI-AI-Display-800x480-IPS-Touch-Screen/blob/421a2d8c797f04151337ffd3ae03ad50bee0ac28/readme.md) explicitly makes this an RGB-parallel display at 800×480, **not DSI**. Touch is GT911. Can be considered only if an RGB display adaptation is in scope; no 1:1 DSI support claim.
- **LilyGO v2.0 evaluation preview:** schematics in manufacturer's repo do not mean this is commercially shipping. Keep separate from v1.0 sales.
- **Other Waveshare screen sizes, Elecrow 9/10.1, Olimex P4-PC, Elecrow/Guition/JC, Makerfabs, Adafruit and other brands:** mostly discovery/inventory placeholders; these are **not verified individual purchasable variants** and do not inherit values from different board packages.

## Source precedence, conflict risks, and product decision

Circuit safety first: confirm the active USB connector and VBUS source, its load switch and current limit, backfeed and ground, and the board's voltage rails before a powered RTL-SDR test. Check exact C6 SDIO CLK/CMD/D0–D3 and enable/reset; verify *both* P4 host and actual C6 coprocessor firmware versions. Display controller detection and panel reset/expander power sequencing should be per-panel and per-PCB version. Treat mixed code and schematic information as `CONFLICT` until resolved, never blindly copy pin maps from adjacent product families.

## Readiness statement

**No board has been demonstrated to meet the full OrcSDR acceptance gate in this web session.** Prior OrcSDR experience and prior REA findings are not a substitute for newly collected serial-number/revision/build/power evidence. The catalog JSON is intended for incremental, citation-preserving updates by the local RSA session and separate OrcSDR documentation integrator.