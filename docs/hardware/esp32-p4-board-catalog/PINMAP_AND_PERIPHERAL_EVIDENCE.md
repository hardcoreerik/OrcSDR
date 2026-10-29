# Pinmaps and peripherals — field-by-field evidence

Sources have pinned commits in every GitHub link; a field tagged `DOCUMENTED_IN_SDK` is a vendor code constant, not a continuity test. `VERIFIED_DOCUMENTARY` means the vendor publicly documents it, not that we physically verified it. Source `board_revision` may explicitly say "PCB unknown". See JSON for complete machine-readable metadata.

## Espressif: Function-EV — `v1.4`

| Field | Extracted value | Evidence status | Primary evidence |
|---|---|---|---|
| `audio.i2s` | `{"mclk": 13, "sclk": 12, "lrclk": 10, "dout": 9, "din": 11, "amp": 53}` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/esp32_p4_function_ev_board/include/bsp/esp32_p4_function_ev_board.h) |
| `i2c.bus` | `{"sda": 7, "scl": 8}` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/esp32_p4_function_ev_board/include/bsp/esp32_p4_function_ev_board.h) |
| `sdk.idf_minimum` | `>=5.4` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/esp32_p4_function_ev_board/idf_component.yml) |
| `sdmmc.pins` | `{"d0": 39, "d1": 40, "d2": 41, "d3": 42, "cmd": 44, "clk": 43}` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/esp32_p4_function_ev_board/include/bsp/esp32_p4_function_ev_board.h) |

## Espressif: Function-EV — `v1.5.2`

| Field | Extracted value | Evidence status | Primary evidence |
|---|---|---|---|
| `audio.i2s` | `{"mclk": 13, "sclk": 12, "lrclk": 10, "dout": 9, "din": 11, "amp": 53}` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/esp32_p4_function_ev_board/include/bsp/esp32_p4_function_ev_board.h) |
| `i2c.bus` | `{"sda": 7, "scl": 8}` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/esp32_p4_function_ev_board/include/bsp/esp32_p4_function_ev_board.h) |
| `sdk.idf_minimum` | `>=5.4` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/esp32_p4_function_ev_board/idf_component.yml) |
| `sdmmc.pins` | `{"d0": 39, "d1": 40, "d2": 41, "d3": 42, "cmd": 44, "clk": 43}` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/esp32_p4_function_ev_board/include/bsp/esp32_p4_function_ev_board.h) |

## M5Stack: Tab5 ILI9881C+GT911 — `panel variant 1; PCB revision unknown`

| Field | Extracted value | Evidence status | Primary evidence |
|---|---|---|---|
| `audio.i2s` | `{"mclk": 30, "bclk": 27, "ws": 29, "dout": 26, "din": 28}` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/include/bsp/m5stack_tab5.h) |
| `display.controller` | `ILI9881C` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/src/bsp_display.c) |
| `display.interface` | `MIPI-DSI` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/src/bsp_display.c) |
| `display.resolution` | `[1280, 720]` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/include/bsp/m5stack_tab5.h) |
| `i2c.bus` | `{"sda": 31, "scl": 32}` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/include/bsp/m5stack_tab5.h) |
| `peripherals.expander` | `{"addresses": "low/high PI4IOE5V6408 addresses (numeric address NOT extracted in this pass)", "lcd_en": 4, "touch_en": 5, "speaker_en": 1, "camera_en": 6, "usb_en_expander1": 3, "c6_en_expander1": 0}` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/include/bsp/m5stack_tab5.h) |
| `sdk.idf_minimum` | `>=5.4` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/idf_component.yml) |
| `sdmmc.pins` | `{"d0": 39, "d1": 40, "d2": 41, "d3": 42, "clk": 43, "cmd": 44}` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/include/bsp/m5stack_tab5.h) |
| `source.license` | `Apache-2.0 for Espressif BSP` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/idf_component.yml) |
| `touch.controller` | `GT911` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/src/bsp_display.c) |
| `touch.interrupt` | `23` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/include/bsp/m5stack_tab5.h) |
| `usb.host_port` | `USB host library installed by bsp_usb_host_start; physical VBUS not bench tested` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/src/bsp_usb.c) |
| `usb.vbus_enable` | `{"expander_index": 1, "bit": 3}` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/include/bsp/m5stack_tab5.h) |

## M5Stack: Tab5 ST7123 — `panel variant 2; PCB revision unknown`

| Field | Extracted value | Evidence status | Primary evidence |
|---|---|---|---|
| `audio.i2s` | `{"mclk": 30, "bclk": 27, "ws": 29, "dout": 26, "din": 28}` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/include/bsp/m5stack_tab5.h) |
| `display.controller` | `ST7123` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/src/bsp_display.c) |
| `display.interface` | `MIPI-DSI` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/src/bsp_display.c) |
| `display.resolution` | `[1280, 720]` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/include/bsp/m5stack_tab5.h) |
| `i2c.bus` | `{"sda": 31, "scl": 32}` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/include/bsp/m5stack_tab5.h) |
| `peripherals.expander` | `{"addresses": "low/high PI4IOE5V6408 addresses (numeric address NOT extracted in this pass)", "lcd_en": 4, "touch_en": 5, "speaker_en": 1, "camera_en": 6, "usb_en_expander1": 3, "c6_en_expander1": 0}` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/include/bsp/m5stack_tab5.h) |
| `sdk.idf_minimum` | `>=5.4` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/idf_component.yml) |
| `sdmmc.pins` | `{"d0": 39, "d1": 40, "d2": 41, "d3": 42, "clk": 43, "cmd": 44}` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/include/bsp/m5stack_tab5.h) |
| `source.license` | `Apache-2.0 for Espressif BSP` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/idf_component.yml) |
| `touch.controller` | `ST7123` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/src/bsp_display.c) |
| `touch.interrupt` | `23` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/include/bsp/m5stack_tab5.h) |
| `usb.host_port` | `USB host library installed by bsp_usb_host_start; physical VBUS not bench tested` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/src/bsp_usb.c) |
| `usb.vbus_enable` | `{"expander_index": 1, "bit": 3}` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/include/bsp/m5stack_tab5.h) |

## M5Stack: Tab5 ST7121 — `panel variant 3; PCB revision unknown`

| Field | Extracted value | Evidence status | Primary evidence |
|---|---|---|---|
| `audio.i2s` | `{"mclk": 30, "bclk": 27, "ws": 29, "dout": 26, "din": 28}` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/include/bsp/m5stack_tab5.h) |
| `display.controller` | `ST7121` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/src/bsp_display.c) |
| `display.interface` | `MIPI-DSI` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/src/bsp_display.c) |
| `display.resolution` | `[1280, 720]` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/include/bsp/m5stack_tab5.h) |
| `i2c.bus` | `{"sda": 31, "scl": 32}` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/include/bsp/m5stack_tab5.h) |
| `peripherals.expander` | `{"addresses": "low/high PI4IOE5V6408 addresses (numeric address NOT extracted in this pass)", "lcd_en": 4, "touch_en": 5, "speaker_en": 1, "camera_en": 6, "usb_en_expander1": 3, "c6_en_expander1": 0}` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/include/bsp/m5stack_tab5.h) |
| `sdk.idf_minimum` | `>=5.4` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/idf_component.yml) |
| `sdmmc.pins` | `{"d0": 39, "d1": 40, "d2": 41, "d3": 42, "clk": 43, "cmd": 44}` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/include/bsp/m5stack_tab5.h) |
| `source.license` | `Apache-2.0 for Espressif BSP` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/idf_component.yml) |
| `touch.controller` | `ST712x-compatible` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/src/bsp_display.c) |
| `touch.interrupt` | `23` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/include/bsp/m5stack_tab5.h) |
| `usb.host_port` | `USB host library installed by bsp_usb_host_start; physical VBUS not bench tested` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/src/bsp_usb.c) |
| `usb.vbus_enable` | `{"expander_index": 1, "bit": 3}` | `DOCUMENTED_IN_SDK` | [source](https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/include/bsp/m5stack_tab5.h) |

## M5Stack: Stamp-P4 — `unverified`

| Field | Extracted value | Evidence status | Primary evidence |
|---|---|---|---|
| `display.interface` | `MIPI-DSI exposed module interface; no integrated panel` | `VERIFIED_DOCUMENTARY` | [source](https://docs.m5stack.com/en/core/Stamp-P4) |
| `usb.host_port` | `USB2 OTG D+/D- breakout; external 5V VBUS supply design unverified` | `VERIFIED_DOCUMENTARY` | [source](https://docs.m5stack.com/en/core/Stamp-P4) |

## M5Stack: Stamp-P4 + AddOn C6 — `unverified`

| Field | Extracted value | Evidence status | Primary evidence |
|---|---|---|---|
| `c6.part` | `Stamp AddOn C6, ESP32-C6 SDIO link claimed` | `VERIFIED_DOCUMENTARY` | [source](https://docs.m5stack.com/en/stamp/Stamp-AddOn_C6_For_P4) |
| `display.interface` | `MIPI-DSI exposed module interface; no integrated panel` | `VERIFIED_DOCUMENTARY` | [source](https://docs.m5stack.com/en/core/Stamp-P4) |
| `usb.host_port` | `USB2 OTG D+/D- breakout; external 5V VBUS supply design unverified` | `VERIFIED_DOCUMENTARY` | [source](https://docs.m5stack.com/en/core/Stamp-P4) |

## M5Stack: Unit PoE-P4 — `unverified`

| Field | Extracted value | Evidence status | Primary evidence |
|---|---|---|---|
| `display.interface` | `MIPI-DSI 24-pin FPC; panel not integrated` | `VERIFIED_DOCUMENTARY` | [source](https://docs.m5stack.com/en/unit/Unit_PoE-P4) |
| `ethernet.phy` | `IP101GRI RMII` | `VERIFIED_DOCUMENTARY` | [source](https://docs.m5stack.com/en/unit/Unit_PoE-P4) |
| `usb.host_port` | `Dedicated Type-C USB 2.0 host and separate Type-C OTG/download` | `VERIFIED_DOCUMENTARY` | [source](https://docs.m5stack.com/en/unit/Unit_PoE-P4) |

## Waveshare: ESP32-P4-Module-DEV-KIT — `unverified`

| Field | Extracted value | Evidence status | Primary evidence |
|---|---|---|---|
| `display.interface` | `DSI connector, panel not automatically included` | `VERIFIED_DOCUMENTARY` | [source](https://docs.waveshare.com/ESP32-P4-Module-DEV-KIT) |
| `usb.host_port` | `USB-A OTG jumper selection; not yet tested with RTL-SDR` | `VERIFIED_DOCUMENTARY` | [source](https://docs.waveshare.com/ESP32-P4-Module-DEV-KIT) |

## Waveshare: ESP32-P4-WIFI6-DEV-KIT — `Rev1.1`

| Field | Extracted value | Evidence status | Primary evidence |
|---|---|---|---|
| `c6.part` | `ESP32-C6 wireless coprocessor, SDIO claimed` | `VERIFIED_DOCUMENTARY` | [source](https://docs.waveshare.com/ESP32-P4-WIFI6-DEV-KIT) |
| `display.interface` | `2-lane MIPI-DSI connector; panel is optional` | `VERIFIED_DOCUMENTARY` | [source](https://docs.waveshare.com/ESP32-P4-WIFI6-DEV-KIT) |
| `usb.host_port` | `USB-A OTG with host/device jumper; port routing differs by PCB revision` | `VERIFIED_DOCUMENTARY` | [source](https://docs.waveshare.com/ESP32-P4-WIFI6-DEV-KIT/FAQ) |
| `usb.jumper_host_selection` | `HOST selects ports 2-4; DEVICE selects port 1` | `VERIFIED_DOCUMENTARY` | [source](https://docs.waveshare.com/ESP32-P4-WIFI6-DEV-KIT/FAQ) |

## Waveshare: ESP32-P4-WIFI6-DEV-KIT — `Rev1.2`

| Field | Extracted value | Evidence status | Primary evidence |
|---|---|---|---|
| `c6.part` | `ESP32-C6 wireless coprocessor, SDIO claimed` | `VERIFIED_DOCUMENTARY` | [source](https://docs.waveshare.com/ESP32-P4-WIFI6-DEV-KIT) |
| `display.interface` | `2-lane MIPI-DSI connector; panel is optional` | `VERIFIED_DOCUMENTARY` | [source](https://docs.waveshare.com/ESP32-P4-WIFI6-DEV-KIT) |
| `usb.host_port` | `USB-A OTG with host/device jumper; port routing differs by PCB revision` | `VERIFIED_DOCUMENTARY` | [source](https://docs.waveshare.com/ESP32-P4-WIFI6-DEV-KIT/FAQ) |
| `usb.jumper_host_selection` | `HOST selects port 1; DEVICE selects ports 2-4` | `VERIFIED_DOCUMENTARY` | [source](https://docs.waveshare.com/ESP32-P4-WIFI6-DEV-KIT/FAQ) |

## LilyGO: T-Display-P4 4.05 TFT — `v1.0 TFT`

| Field | Extracted value | Evidence status | Primary evidence |
|---|---|---|---|
| `audio.i2s` | `{"codec": "ES8311", "i2c_addr": "0x18", "mclk": 13, "bclk": 12, "ws": 9, "dout": 10, "din": 11}` | `DOCUMENTED_IN_SDK` | [source](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/a47cf73e2c9d442c0f00b618749150f4482dcd0a/src/device/t_display_p4/t_display_p4_config.h) |
| `c6.reset` | `XL9535 IO14 (software declaration)` | `DOCUMENTED_IN_SDK` | [source](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/a47cf73e2c9d442c0f00b618749150f4482dcd0a/src/device/t_display_p4/t_display_p4_config.h) |
| `c6.sdio` | `{"d0": 14, "d1": 15, "d2": 16, "d3": 17, "clk": 18, "cmd": 19}` | `DOCUMENTED_IN_SDK` | [source](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/a47cf73e2c9d442c0f00b618749150f4482dcd0a/src/device/t_display_p4/t_display_p4_config.h) |
| `display.controller` | `HI8561` | `DOCUMENTED_IN_SDK` | [source](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/a47cf73e2c9d442c0f00b618749150f4482dcd0a/src/device/t_display_p4/t_display_p4_config.h) |
| `display.interface` | `MIPI-DSI` | `DOCUMENTED_IN_SDK` | [source](https://github.com/Xinyuan-LilyGO/T-Display-P4/blob/7b021f795f6842fe6d62d3b2f2931cd0f03d3f80/README.md) |
| `display.lanes` | `2` | `DOCUMENTED_IN_SDK` | [source](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/a47cf73e2c9d442c0f00b618749150f4482dcd0a/src/device/t_display_p4/t_display_p4_config.h) |
| `display.resolution` | `[540, 1168]` | `DOCUMENTED_IN_SDK` | [source](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/a47cf73e2c9d442c0f00b618749150f4482dcd0a/src/device/t_display_p4/t_display_p4_config.h) |
| `ethernet.phy` | `{"part": "IP101", "mdc": 31, "mdio": 52, "tx_en": 49, "rxd0": 29, "rxd1": 30, "txd0": 34, "txd1": 35, "refclk": 50}` | `DOCUMENTED_IN_SDK` | [source](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/a47cf73e2c9d442c0f00b618749150f4482dcd0a/src/device/t_display_p4/t_display_p4_config.h) |
| `i2c.bus` | `{"bus1_sda": 7, "bus1_scl": 8, "bus2_sda": 20, "bus2_scl": 21}` | `DOCUMENTED_IN_SDK` | [source](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/a47cf73e2c9d442c0f00b618749150f4482dcd0a/src/device/t_display_p4/t_display_p4_config.h) |
| `peripherals.expander` | `{"type": "XL9535", "address": "0x20", "screen_rst": "IO2", "touch_rst": "IO3", "touch_int": "IO4", "usb_phy_power": "IO10", "esp32c6_en": "IO14", "sd_power": "IO15", "radio_rst": "IO16", "radio_dio1": "IO17"}` | `DOCUMENTED_IN_SDK` | [source](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/a47cf73e2c9d442c0f00b618749150f4482dcd0a/src/device/t_display_p4/t_display_p4_config.h) |
| `sdk.idf_minimum` | `>=5.5.4` | `DOCUMENTED_IN_SDK` | [source](https://github.com/Xinyuan-LilyGO/T-Display-P4/blob/7b021f795f6842fe6d62d3b2f2931cd0f03d3f80/main/idf_component.yml) |
| `sdmmc.pins` | `{"d0": 39, "d1": 40, "d2": 41, "d3": 42, "clk": 43, "cmd": 44}` | `DOCUMENTED_IN_SDK` | [source](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/a47cf73e2c9d442c0f00b618749150f4482dcd0a/src/device/t_display_p4/t_display_p4_config.h) |
| `source.license` | `GPL-3.0` | `DOCUMENTED_IN_SDK` | [source](https://github.com/Xinyuan-LilyGO/T-Display-P4/blob/7b021f795f6842fe6d62d3b2f2931cd0f03d3f80/README.md) |
| `touch.controller` | `HI8561 integrated touch` | `DOCUMENTED_IN_SDK` | [source](https://github.com/Xinyuan-LilyGO/T-Display-P4/blob/7b021f795f6842fe6d62d3b2f2931cd0f03d3f80/README.md) |
| `touch.i2c_address` | `0x68` | `DOCUMENTED_IN_SDK` | [source](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/a47cf73e2c9d442c0f00b618749150f4482dcd0a/src/device/t_display_p4/t_display_p4_config.h) |
| `usb.vbus_enable` | `XL9535 IO10 is named USB PHY power enable; VBUS 5V switching must be separately verified` | `DOCUMENTED_IN_SDK` | [source](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/a47cf73e2c9d442c0f00b618749150f4482dcd0a/src/device/t_display_p4/t_display_p4_config.h) |

## LilyGO: T-Display-P4 4.1 AMOLED — `v1.0 AMOLED`

| Field | Extracted value | Evidence status | Primary evidence |
|---|---|---|---|
| `audio.i2s` | `{"codec": "ES8311", "i2c_addr": "0x18", "mclk": 13, "bclk": 12, "ws": 9, "dout": 10, "din": 11}` | `DOCUMENTED_IN_SDK` | [source](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/a47cf73e2c9d442c0f00b618749150f4482dcd0a/src/device/t_display_p4/t_display_p4_config.h) |
| `c6.reset` | `XL9535 IO14 (software declaration)` | `DOCUMENTED_IN_SDK` | [source](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/a47cf73e2c9d442c0f00b618749150f4482dcd0a/src/device/t_display_p4/t_display_p4_config.h) |
| `c6.sdio` | `{"d0": 14, "d1": 15, "d2": 16, "d3": 17, "clk": 18, "cmd": 19}` | `DOCUMENTED_IN_SDK` | [source](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/a47cf73e2c9d442c0f00b618749150f4482dcd0a/src/device/t_display_p4/t_display_p4_config.h) |
| `display.controller` | `RM69A10` | `DOCUMENTED_IN_SDK` | [source](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/a47cf73e2c9d442c0f00b618749150f4482dcd0a/src/device/t_display_p4/t_display_p4_config.h) |
| `display.interface` | `MIPI-DSI` | `DOCUMENTED_IN_SDK` | [source](https://github.com/Xinyuan-LilyGO/T-Display-P4/blob/7b021f795f6842fe6d62d3b2f2931cd0f03d3f80/README.md) |
| `display.lanes` | `2` | `DOCUMENTED_IN_SDK` | [source](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/a47cf73e2c9d442c0f00b618749150f4482dcd0a/src/device/t_display_p4/t_display_p4_config.h) |
| `display.resolution` | `[568, 1232]` | `DOCUMENTED_IN_SDK` | [source](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/a47cf73e2c9d442c0f00b618749150f4482dcd0a/src/device/t_display_p4/t_display_p4_config.h) |
| `ethernet.phy` | `{"part": "IP101", "mdc": 31, "mdio": 52, "tx_en": 49, "rxd0": 29, "rxd1": 30, "txd0": 34, "txd1": 35, "refclk": 50}` | `DOCUMENTED_IN_SDK` | [source](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/a47cf73e2c9d442c0f00b618749150f4482dcd0a/src/device/t_display_p4/t_display_p4_config.h) |
| `i2c.bus` | `{"bus1_sda": 7, "bus1_scl": 8, "bus2_sda": 20, "bus2_scl": 21}` | `DOCUMENTED_IN_SDK` | [source](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/a47cf73e2c9d442c0f00b618749150f4482dcd0a/src/device/t_display_p4/t_display_p4_config.h) |
| `peripherals.expander` | `{"type": "XL9535", "address": "0x20", "screen_rst": "IO2", "touch_rst": "IO3", "touch_int": "IO4", "usb_phy_power": "IO10", "esp32c6_en": "IO14", "sd_power": "IO15", "radio_rst": "IO16", "radio_dio1": "IO17"}` | `DOCUMENTED_IN_SDK` | [source](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/a47cf73e2c9d442c0f00b618749150f4482dcd0a/src/device/t_display_p4/t_display_p4_config.h) |
| `sdk.idf_minimum` | `>=5.5.4` | `DOCUMENTED_IN_SDK` | [source](https://github.com/Xinyuan-LilyGO/T-Display-P4/blob/7b021f795f6842fe6d62d3b2f2931cd0f03d3f80/main/idf_component.yml) |
| `sdmmc.pins` | `{"d0": 39, "d1": 40, "d2": 41, "d3": 42, "clk": 43, "cmd": 44}` | `DOCUMENTED_IN_SDK` | [source](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/a47cf73e2c9d442c0f00b618749150f4482dcd0a/src/device/t_display_p4/t_display_p4_config.h) |
| `source.license` | `GPL-3.0` | `DOCUMENTED_IN_SDK` | [source](https://github.com/Xinyuan-LilyGO/T-Display-P4/blob/7b021f795f6842fe6d62d3b2f2931cd0f03d3f80/README.md) |
| `touch.controller` | `GT9895` | `DOCUMENTED_IN_SDK` | [source](https://github.com/Xinyuan-LilyGO/T-Display-P4/blob/7b021f795f6842fe6d62d3b2f2931cd0f03d3f80/README.md) |
| `touch.i2c_address` | `0x5D` | `DOCUMENTED_IN_SDK` | [source](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/a47cf73e2c9d442c0f00b618749150f4482dcd0a/src/device/t_display_p4/t_display_p4_config.h) |
| `usb.vbus_enable` | `XL9535 IO10 is named USB PHY power enable; VBUS 5V switching must be separately verified` | `DOCUMENTED_IN_SDK` | [source](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/a47cf73e2c9d442c0f00b618749150f4482dcd0a/src/device/t_display_p4/t_display_p4_config.h) |

## Olimex: ESP32-P4-DevKit — `Rev D1`

| Field | Extracted value | Evidence status | Primary evidence |
|---|---|---|---|
| `source.license` | `Hardware CERN-OHL-S-2.0, software GPL-3.0, docs CC BY-SA 4.0` | `PREVIOUS_REA_RESULT` | [source](https://github.com/hardcoreerik/REA-Results/blob/main/targets/esp32-p4-devkit/notes/pcb-lessons-2026-10-16.md) |

## Seeed Studio: reTerminal D1001 — `mainboard v1.0 per docs`

| Field | Extracted value | Evidence status | Primary evidence |
|---|---|---|---|
| `audio.i2s` | `ES8311 + ES7210` | `VERIFIED_DOCUMENTARY` | [source](https://wiki.seeedstudio.com/getting_started_with_reterminal_d1001/) |
| `c6.part` | `ESP32-C6 over SDIO` | `VERIFIED_DOCUMENTARY` | [source](https://wiki.seeedstudio.com/getting_started_with_reterminal_d1001/) |
| `display.controller` | `9365DA-H3` | `VERIFIED_DOCUMENTARY` | [source](https://wiki.seeedstudio.com/getting_started_with_reterminal_d1001/) |
| `display.interface` | `MIPI-DSI` | `VERIFIED_DOCUMENTARY` | [source](https://wiki.seeedstudio.com/getting_started_with_reterminal_d1001/) |
| `display.resolution` | `[800, 1280]` | `VERIFIED_DOCUMENTARY` | [source](https://wiki.seeedstudio.com/getting_started_with_reterminal_d1001/) |
| `ethernet.phy` | `not advertised` | `UNVERIFIED` | [source](https://wiki.seeedstudio.com/getting_started_with_reterminal_d1001/) |
| `touch.controller` | `GSL3670` | `VERIFIED_DOCUMENTARY` | [source](https://wiki.seeedstudio.com/getting_started_with_reterminal_d1001/) |

## DFRobot: FireBeetle 2 ESP32-P4 — `unverified; early lot requires C6 firmware check`

| Field | Extracted value | Evidence status | Primary evidence |
|---|---|---|---|
| `c6.reset` | `54` | `VERIFIED_DOCUMENTARY` | [source](https://wiki.dfrobot.com/dfr1172/) |
| `c6.sdio` | `{"d0": 14, "d1": 15, "d2": 16, "d3": 17, "clk": 18, "cmd": 19}` | `VERIFIED_DOCUMENTARY` | [source](https://wiki.dfrobot.com/dfr1172/) |
| `display.interface` | `2-lane DSI connector; installed display not proven` | `VERIFIED_DOCUMENTARY` | [source](https://wiki.dfrobot.com/dfr1172/) |
| `sdmmc.pins` | `{"d0": 39, "d1": 40, "d2": 41, "d3": 42, "clk": 43, "cmd": 44, "en": 45}` | `VERIFIED_DOCUMENTARY` | [source](https://wiki.dfrobot.com/dfr1172/) |
| `source.license` | `Public manufacturer wiki; example software license not checked` | `UNVERIFIED` | [source](https://wiki.dfrobot.com/dfr1172/) |
| `usb.host_port` | `Separate high-speed USB-C OTG; VBUS sourcing not yet proved` | `VERIFIED_DOCUMENTARY` | [source](https://wiki.dfrobot.com/dfr1172/) |

## Elecrow: CrowPanel Advanced 5-inch — `v1.0`

| Field | Extracted value | Evidence status | Primary evidence |
|---|---|---|---|
| `display.interface` | `RGB parallel (NOT MIPI DSI)` | `VERIFIED_DOCUMENTARY` | [source](https://github.com/Elecrow-RD/-CrowPanel-Advanced-5inch-ESP32-P4-HMI-AI-Display-800x480-IPS-Touch-Screen/blob/421a2d8c797f04151337ffd3ae03ad50bee0ac28/readme.md) |
| `display.resolution` | `[800, 480]` | `VERIFIED_DOCUMENTARY` | [source](https://github.com/Elecrow-RD/-CrowPanel-Advanced-5inch-ESP32-P4-HMI-AI-Display-800x480-IPS-Touch-Screen/blob/421a2d8c797f04151337ffd3ae03ad50bee0ac28/readme.md) |
| `display.rgb_pinmap` | `{"hsync": 40, "vsync": 41, "de": 2, "pclk": 3, "data0_to_15": [8, 7, 6, 5, 4, 14, 13, 12, 11, 10, 9, 19, 18, 17, 16, 15]}` | `VERIFIED_DOCUMENTARY` | [source](https://github.com/Elecrow-RD/-CrowPanel-Advanced-5inch-ESP32-P4-HMI-AI-Display-800x480-IPS-Touch-Screen/blob/421a2d8c797f04151337ffd3ae03ad50bee0ac28/readme.md) |
| `touch.controller` | `GT911` | `VERIFIED_DOCUMENTARY` | [source](https://github.com/Elecrow-RD/-CrowPanel-Advanced-5inch-ESP32-P4-HMI-AI-Display-800x480-IPS-Touch-Screen/blob/421a2d8c797f04151337ffd3ae03ad50bee0ac28/readme.md) |
| `touch.i2c_address` | `0x5D or 0x14 based on reset strap` | `VERIFIED_DOCUMENTARY` | [source](https://github.com/Elecrow-RD/-CrowPanel-Advanced-5inch-ESP32-P4-HMI-AI-Display-800x480-IPS-Touch-Screen/blob/421a2d8c797f04151337ffd3ae03ad50bee0ac28/readme.md) |
| `touch.reset` | `36` | `VERIFIED_DOCUMENTARY` | [source](https://github.com/Elecrow-RD/-CrowPanel-Advanced-5inch-ESP32-P4-HMI-AI-Display-800x480-IPS-Touch-Screen/blob/421a2d8c797f04151337ffd3ae03ad50bee0ac28/readme.md) |

## Elecrow: CrowPanel Advanced 7-inch — `v1.0`

| Field | Extracted value | Evidence status | Primary evidence |
|---|---|---|---|
| `c6.sdio` | `{"d0": 14, "d1": 15, "d2": 16, "d3": 17}` | `VERIFIED_DOCUMENTARY` | [source](https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen/blob/a25ecadb4b4d28e9e904e114a6a1b82ae6f84624/readme.md) |
| `display.interface` | `MIPI-DSI` | `VERIFIED_DOCUMENTARY` | [source](https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen/blob/a25ecadb4b4d28e9e904e114a6a1b82ae6f84624/readme.md) |
| `display.resolution` | `[1024, 600]` | `VERIFIED_DOCUMENTARY` | [source](https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen/blob/a25ecadb4b4d28e9e904e114a6a1b82ae6f84624/Eagle_SCH&PCB/1.2/readme.md) |

## Elecrow: CrowPanel Advanced 7-inch — `v1.1`

| Field | Extracted value | Evidence status | Primary evidence |
|---|---|---|---|
| `c6.sdio` | `{"d0": 17, "d1": 16, "d2": 15, "d3": 14}` | `VERIFIED_DOCUMENTARY` | [source](https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen/blob/a25ecadb4b4d28e9e904e114a6a1b82ae6f84624/readme.md) |
| `display.interface` | `MIPI-DSI` | `VERIFIED_DOCUMENTARY` | [source](https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen/blob/a25ecadb4b4d28e9e904e114a6a1b82ae6f84624/readme.md) |
| `display.resolution` | `[1024, 600]` | `VERIFIED_DOCUMENTARY` | [source](https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen/blob/a25ecadb4b4d28e9e904e114a6a1b82ae6f84624/Eagle_SCH&PCB/1.2/readme.md) |

## Elecrow: CrowPanel Advanced 7-inch — `v1.2`

| Field | Extracted value | Evidence status | Primary evidence |
|---|---|---|---|
| `audio.i2s` | `{"lrclk": 21, "bclk": 22, "data": 23, "amp_control": 30}` | `VERIFIED_DOCUMENTARY` | [source](https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen/blob/a25ecadb4b4d28e9e904e114a6a1b82ae6f84624/Eagle_SCH&PCB/1.2/readme.md) |
| `c6.reset` | `32` | `VERIFIED_DOCUMENTARY` | [source](https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen/blob/a25ecadb4b4d28e9e904e114a6a1b82ae6f84624/Eagle_SCH&PCB/1.2/readme.md) |
| `c6.sdio` | `{"d0": 17, "d1": 16, "d2": 15, "d3": 14}` | `VERIFIED_DOCUMENTARY` | [source](https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen/blob/a25ecadb4b4d28e9e904e114a6a1b82ae6f84624/readme.md) |
| `display.controller` | `EK79007` | `VERIFIED_DOCUMENTARY` | [source](https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen/blob/a25ecadb4b4d28e9e904e114a6a1b82ae6f84624/Eagle_SCH&PCB/1.2/readme.md) |
| `display.interface` | `MIPI-DSI` | `VERIFIED_DOCUMENTARY` | [source](https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen/blob/a25ecadb4b4d28e9e904e114a6a1b82ae6f84624/readme.md) |
| `display.lanes` | `2` | `VERIFIED_DOCUMENTARY` | [source](https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen/blob/a25ecadb4b4d28e9e904e114a6a1b82ae6f84624/Eagle_SCH&PCB/1.2/readme.md) |
| `display.resolution` | `[1024, 600]` | `VERIFIED_DOCUMENTARY` | [source](https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen/blob/a25ecadb4b4d28e9e904e114a6a1b82ae6f84624/Eagle_SCH&PCB/1.2/readme.md) |
| `i2c.bus` | `{"sda": 45, "scl": 46}` | `VERIFIED_DOCUMENTARY` | [source](https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen/blob/a25ecadb4b4d28e9e904e114a6a1b82ae6f84624/Eagle_SCH&PCB/1.2/readme.md) |
| `sdmmc.pins` | `{"schematic": {"d3": 14, "d2": 15, "d1": 16, "d0": 17, "clk": 18, "cmd": 19}, "example_code": {"d0": 39, "clk": 43, "cmd": 44}}` | `CONFLICT` | [source](https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen/blob/a25ecadb4b4d28e9e904e114a6a1b82ae6f84624/Eagle_SCH&PCB/1.2/readme.md) |
| `touch.controller` | `GT911` | `VERIFIED_DOCUMENTARY` | [source](https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen/blob/a25ecadb4b4d28e9e904e114a6a1b82ae6f84624/Eagle_SCH&PCB/1.2/readme.md) |
| `touch.i2c_address` | `0x5D or 0x14, reset/INT strap dependent` | `VERIFIED_DOCUMENTARY` | [source](https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen/blob/a25ecadb4b4d28e9e904e114a6a1b82ae6f84624/readme.md) |

## Discipline

These snapshots may contain inherited BSP pin constants. Do not assume a generic ESP32-P4 peripheral or neighboring board is electrically connected the same way. When schematic and compiled code disagree, preserve **both** under `CONFLICT`; the actual board and runtime test decide. Any blank field is explicitly `UNVERIFIED` in the JSON.