# Manufacturer SDK investigation queue — draft

Snapshot 2026-10-28; imported = 0, built = 0 in this session. This file was missing from the OrcSDR documentation branch and is intended as a reviewed replacement. A source URL/commit is a research reference; it is **not evidence that `rsa.py` imported or built it**.

| Priority | Source / scope | Pinned source commit | License gate | Immediate extraction / build probe |
|---|---|---|---|---|
| P0 | `espressif/esp-bsp` | `9112bd334726368d4aa4421dd01f2e4b1a739568` | `Apache-2.0` | Tab5 3 panel selectors; Function-EV USB and pin differences |
| P0 | `espressif/esp-hosted-mcu` | `78b1c1c43c02b0c4e898cf87f94679278b1ddaff` | `Apache-2.0` | C6 SDIO host/CP versions and IDF 5.5 compatibility |
| P0 | `Xinyuan-LilyGO/T-Display-P4` | `7b021f795f6842fe6d62d3b2f2931cd0f03d3f80` | `GPL-3.0` | TFT vs AMOLED; ESP32C6 AT-SDIO transport; 5.5.4 build |
| P0 | `Xinyuan-LilyGO/lilygo_device_driver` | `a47cf73e2c9d442c0f00b618749150f4482dcd0a` | `GPL-3.0` | XL9535 + 2 SDIO buses + panel/touch |
| P1 | `OLIMEX/ESP32-P4-DevKit` | `26705d36407a07324348927dfd30fbf4ffc1d94c` | `Software GPL-3.0; Hardware CERN-OHL-S-2.0; docs CC-BY-SA-4.0` | Use existing PCB REA snapshot, then build mipi_dsi |
| P1 | `m5stack/M5GFX` | `6e53f9bfcf7dae34e7c7c089cfd66db977793e17` | `NOT_CHECKED` | License check, Tab5 draw/driver port |
| P1 | `m5stack/M5Unified` | `565a499675b4cbbf897451d7f9ef66890e2cbb90` | `NOT_CHECKED` | License check, Tab5 board detection |
| P1 | `Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen` | `a25ecadb4b4d28e9e904e114a6a1b82ae6f84624` | `UNVERIFIED` | License approval before SDK import; v1.0/v1.1 SDIO and v1.2 SD conflicts |
| P1 | `Elecrow-RD/-CrowPanel-Advanced-5inch-ESP32-P4-HMI-AI-Display-800x480-IPS-Touch-Screen` | `421a2d8c797f04151337ffd3ae03ad50bee0ac28` | `UNVERIFIED` | License approval before SDK import; RGB backend |

## Documentation-first manufacturers without pinned open-source repo

- **Waveshare:** official product wiki & schematic downloads; exact kit/PCB/panel SKU, USB jumper and VBUS trace. Identify sample repo license before import.
- **DFRobot:** DFR1172 official pinout, coprocessor firmware early-lot caveat; locate license-controlled example source.
- **Seeed Studio:** reTerminal D1001 screen/touch/C6; verify USB Host support, source+schematics license.
- **Adafruit:** Metro Hydra specific version, schematic/open example; do not infer panel or C6.
- **Makerfabs:** MaTouch ESP32-P4 models, exact panel/PCB and official BSP.
- **Guition / JC / Sunton / Elecrow 9/10.1:** official factory SKU/PCB; locate schematic and licensed examples.
- **Olimex P4-PC, SparkFun, Heltec, Banana Pi, Orange Pi, Hosyond:** only add as distinct catalog products once a primary real ESP32-P4 board is confirmed.

## Board-first test priority

1. Tab5 exact panel variant: DSI + touch power, C6 Host/CP, USB host VBUS and RTL-SDR sustained I/Q.
2. LilyGO v1.0 TFT and AMOLED independently: XL9535 power, SDIO AT-versus-ESP-Hosted, host USB VBUS.
3. Elecrow 7-inch exact rev: raw netlist vs sample GPIO, SDIO wiring and USB Host physical circuit.
4. Waveshare WIFI6 Rev1.1 and Rev1.2 separately: USB port jumper polarity and VBUS.
5. DFRobot FireBeetle2 and M5 Stamp/PoE-P4: headless benchmark, peripheral availability, powered USB host.

At each stage, run the next manufacturer only when license and reproducibility receipts are captured. Physical testing is separate authorization.