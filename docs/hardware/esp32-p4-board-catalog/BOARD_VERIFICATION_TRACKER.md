# Board verification tracker — source-based draft

Snapshot: 2026-10-28. Owner/session: ChatGPT web-chat SDK/source documentary pass. **Checkboxes indicate only cited source extraction, not working hardware.** Signoff is blank; all gates beyond source/pinmap are unpassed. Product existence and price/stock are not established for discovery placeholders.

| Board ID | Rev / assembly | Stage | Source receipt | Selected pins | IDF build | Hardware | Profile signoff | Next |
|---|---|---|---|---|---|---|---|---|
| `espressif-function-ev-v1.4` | v1.4 | `SOURCE_CAPTURED` | ☑ | ☐ | ☐ | ☐ | ☐ | Verify PCB/panel; powered USB + C6 CP/host |
| `espressif-function-ev-v1.5.2` | v1.5.2 | `SOURCE_CAPTURED` | ☑ | ☐ | ☐ | ☐ | ☐ | Verify PCB/panel; powered USB + C6 CP/host |
| `espressif-function-ev-p4x` | variant unconfirmed | `DISCOVERY_PENDING` | ☐ | ☐ | ☐ | ☐ | ☐ | Find official board + revision source |
| `espressif-function-ev-c5` | variant unconfirmed | `DISCOVERY_PENDING` | ☐ | ☐ | ☐ | ☐ | ☐ | Find official board + revision source |
| `espressif-p4-eye` | unverified | `IDENTIFIED` | ☐ | ☐ | ☐ | ☐ | ☐ | Find official board + revision source |
| `espressif-p4-core` | unverified | `DISCOVERY_PENDING` | ☐ | ☐ | ☐ | ☐ | ☐ | Find official board + revision source |
| `m5-tab5-ili9881c` | panel variant 1; PCB revision unknown | `PINMAP_EXTRACTED` | ☑ | ☑ | ☐ | ☐ | ☐ | Verify PCB/panel; powered USB + C6 CP/host |
| `m5-tab5-st7123` | panel variant 2; PCB revision unknown | `PINMAP_EXTRACTED` | ☑ | ☑ | ☐ | ☐ | ☐ | Verify PCB/panel; powered USB + C6 CP/host |
| `m5-tab5-st7121` | panel variant 3; PCB revision unknown | `PINMAP_EXTRACTED` | ☑ | ☑ | ☐ | ☐ | ☐ | Verify PCB/panel; powered USB + C6 CP/host |
| `m5-stamp-p4` | unverified | `SOURCE_CAPTURED` | ☑ | ☐ | ☐ | ☐ | ☐ | Verify PCB/panel; powered USB + C6 CP/host |
| `m5-stamp-p4-c6` | unverified | `SOURCE_CAPTURED` | ☑ | ☐ | ☐ | ☐ | ☐ | Verify PCB/panel; powered USB + C6 CP/host |
| `m5-unit-poe-p4` | unverified | `SOURCE_CAPTURED` | ☑ | ☐ | ☐ | ☐ | ☐ | Verify PCB/panel; powered USB + C6 CP/host |
| `waveshare-module-devkit` | unverified | `SOURCE_CAPTURED` | ☑ | ☐ | ☐ | ☐ | ☐ | Verify PCB/panel; powered USB + C6 CP/host |
| `waveshare-wifi6-devkit-rev1.1` | Rev1.1 | `SOURCE_CAPTURED` | ☑ | ☐ | ☐ | ☐ | ☐ | Verify PCB/panel; powered USB + C6 CP/host |
| `waveshare-wifi6-devkit-rev1.2` | Rev1.2 | `SOURCE_CAPTURED` | ☑ | ☐ | ☐ | ☐ | ☐ | Verify PCB/panel; powered USB + C6 CP/host |
| `waveshare-wifi6-eth-poe` | unverified | `DISCOVERY_PENDING` | ☐ | ☐ | ☐ | ☐ | ☐ | Find official board + revision source |
| `waveshare-3p5-spi` | unverified | `DISCOVERY_PENDING` | ☐ | ☐ | ☐ | ☐ | ☐ | Find official board + revision source |
| `waveshare-4p3-dsi` | unverified | `DISCOVERY_PENDING` | ☐ | ☐ | ☐ | ☐ | ☐ | Find official board + revision source |
| `waveshare-4b-dsi` | unverified | `DISCOVERY_PENDING` | ☐ | ☐ | ☐ | ☐ | ☐ | Find official board + revision source |
| `waveshare-5-dsi` | unverified | `DISCOVERY_PENDING` | ☐ | ☐ | ☐ | ☐ | ☐ | Find official board + revision source |
| `waveshare-7-dsi` | unverified | `DISCOVERY_PENDING` | ☐ | ☐ | ☐ | ☐ | ☐ | Find official board + revision source |
| `waveshare-8-dsi` | unverified | `DISCOVERY_PENDING` | ☐ | ☐ | ☐ | ☐ | ☐ | Find official board + revision source |
| `waveshare-10p1-dsi` | unverified | `DISCOVERY_PENDING` | ☐ | ☐ | ☐ | ☐ | ☐ | Find official board + revision source |
| `lilygo-p4-tft-v1` | v1.0 TFT | `PINMAP_EXTRACTED` | ☑ | ☑ | ☐ | ☐ | ☐ | Verify PCB/panel; powered USB + C6 CP/host |
| `lilygo-p4-amoled-v1` | v1.0 AMOLED | `PINMAP_EXTRACTED` | ☑ | ☑ | ☐ | ☐ | ☐ | Verify PCB/panel; powered USB + C6 CP/host |
| `lilygo-p4-v2-eval` | evaluation preview | `DISCOVERY_PENDING` | ☐ | ☐ | ☐ | ☐ | ☐ | Find official board + revision source |
| `lilygo-p4-keyboard` | v1.0 expansion | `IDENTIFIED` | ☐ | ☐ | ☐ | ☐ | ☐ | Find official board + revision source |
| `olimex-p4-devkit-d1` | Rev D1 | `SOURCE_CAPTURED` | ☑ | ☐ | ☐ | ☐ | ☐ | Verify PCB/panel; powered USB + C6 CP/host |
| `olimex-p4-pc` | unverified | `DISCOVERY_PENDING` | ☐ | ☐ | ☐ | ☐ | ☐ | Find official board + revision source |
| `seeed-d1001` | mainboard v1.0 per docs | `SOURCE_CAPTURED` | ☑ | ☐ | ☐ | ☐ | ☐ | Verify PCB/panel; powered USB + C6 CP/host |
| `dfrobot-dfr1172` | unverified; early lot requires C6 firmware check | `SOURCE_CAPTURED` | ☑ | ☐ | ☐ | ☐ | ☐ | Verify PCB/panel; powered USB + C6 CP/host |
| `adafruit-metro-hydra` | unverified | `DISCOVERY_PENDING` | ☐ | ☐ | ☐ | ☐ | ☐ | Find official board + revision source |
| `makerfabs-matouch-p4` | unverified | `DISCOVERY_PENDING` | ☐ | ☐ | ☐ | ☐ | ☐ | Find official board + revision source |
| `elecrow-5-v1` | v1.0 | `PINMAP_EXTRACTED` | ☑ | ☑ | ☐ | ☐ | ☐ | Verify PCB/panel; powered USB + C6 CP/host |
| `elecrow-7-v1.0` | v1.0 | `SOURCE_CAPTURED` | ☑ | ☐ | ☐ | ☐ | ☐ | Verify PCB/panel; powered USB + C6 CP/host |
| `elecrow-7-v1.1` | v1.1 | `SOURCE_CAPTURED` | ☑ | ☐ | ☐ | ☐ | ☐ | Verify PCB/panel; powered USB + C6 CP/host |
| `elecrow-7-v1.2` | v1.2 | `SOURCE_CAPTURED` | ☑ | ☐ | ☐ | ☐ | ☐ | Verify PCB/panel; powered USB + C6 CP/host |
| `elecrow-9-v1.2` | v1.2 | `IDENTIFIED` | ☐ | ☐ | ☐ | ☐ | ☐ | Find official board + revision source |
| `elecrow-10p1` | unverified | `DISCOVERY_PENDING` | ☐ | ☐ | ☐ | ☐ | ☐ | Find official board + revision source |
| `guition-jc-esp32p4` | exact model unverified | `DISCOVERY_PENDING` | ☐ | ☐ | ☐ | ☐ | ☐ | Find official board + revision source |

## Individual acceptance checkboxes (repeat per revision when advancing a board)

- [ ] Product/SKU and commercially available revision established by manufacturer and dated stock/retailer page.
- [ ] PCB revision and separately installed screen/panel assembly physically identified.
- [ ] Schematic/netlist, vendor BSP and revision errata individually pinned and licensed.
- [ ] Pin-level I2C/screen/touch/reset/backlight + expander writes cross-referenced with correct PCB revision.
- [ ] Full P4 ↔ C6 SDIO net-level CLK/CMD/D0-D3 + C6 EN/wake/handshake cross-referenced.
- [ ] Actual C6 part and CP image version recorded, host-side ESP-Hosted version identified, transport validated.
- [ ] USB HOST versus DEVICE connectors, HS/FS PHY, VBUS switch, current budget and backfeed verified.
- [ ] Driver sample compiled under recorded IDF 5.5.x commit; stdout/stderr and ELF SHA collected.
- [ ] MIPI screen and touch physically exercised with startup/reset and 1280×720 UI compatibility reviewed.
- [ ] RTL-SDR V4 enumerated on powered host, sustained I/Q proven and thermals measured.
- [ ] SDMMC/eMMC conflicts tested, plus audio/RMII/GNSS/LoRa/IMU where applicable.
- [ ] Firmware runtime logs attached, compatibility score signed off by named engineer.

For each board, missing fields are enumerated in `UNVERIFIED_REGISTER.md`; current partial facts are in `PINMAP_AND_PERIPHERAL_EVIDENCE.md` and the machine-readable JSON. Do not mark a board complete simply because `selected pins` is checked: it only means **some** vendor constants were located.