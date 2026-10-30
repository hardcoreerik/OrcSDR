# ESP32-P4 board profiles in the OrcSDR console firmware

Branch: `claude/p4-board-profiles` (built on `codex/orcsdr-p4-hosyond-console`). Nothing here is merged.
The research branch `research/esp32-p4-board-catalog-20261028` owns `docs/hardware/esp32-p4-board-catalog/`;
this document does not touch it. When that branch publishes its tracker and evidence files, promote a profile
only when its gates are met (see "Promoting a profile").

## What exists

One firmware, one board profile chosen at build time. A profile is data: pins, which peripherals exist, where
each value came from, what is unsupported. Code reads the profile instead of GPIO numbers.

| Piece | File |
|---|---|
| Profile type, status levels | `components/orcsdr_board_hosyond/include/orcsdr_board_profile.h` |
| One file per board | `components/orcsdr_board_hosyond/src/boards/board_*.c` |
| Selector, list, boot log, pin cross-check | `components/orcsdr_board_hosyond/src/orcsdr_profiles.c` |
| Build-time choice | `components/orcsdr_board_hosyond/Kconfig` (`ORCSDR_BOARD`) |
| Display panels (separate from boards) | `include/orcsdr_display_profile.h` |

All profiles are compiled into every build, so a typo in any board file breaks every build. `ORCSDR_BOARD` only
selects which one `orcsdr_board_profile()` returns. At boot the firmware logs the profile, its status and what is
unsupported, and logs an error if the ESP-Hosted SDIO pins in `sdkconfig` disagree with the profile.

Status levels, never mixed:

- **bench-verified**: exercised on the real board with this firmware.
- **documented-only**: taken from a vendor schematic, vendor BSP or Espressif BSP. Not measured. Treat as a lead.
- **skeleton**: identity only.

## Support matrix

| Board | Profile status | Console (screen + touch) | Wi-Fi (C6 SDIO) | USB host (RTL-SDR) | SD | Ethernet |
|---|---|---|---|---|---|---|
| Waveshare ESP32-P4-Module-DEV-KIT + Hosyond 7" 800x480 | bench-verified (display, audio, C6 link, USB host); SD and Ethernet not exercised by this firmware | works | pins match; see "Open item" | works (jumper selects host) | never mounted a card here (power switch GPIO45 from the schematic) | IP101 RMII present; OrcNode runs it; not wired into the console |
| Waveshare ESP32-P4-WIFI6-DB (probably the COM29 board) | documented-only; I2C codec and USB host observed once | headless runs; panel untested | **ESP32-C5**: embedded image is C6-only, Wi-Fi down | via 4-pin header, an RTL-SDR streamed | power switch GPIO45 (low = on), no card tested | none |
| Waveshare ESP32-P4-WIFI6-POE-ETH | documented-only | not run | pins documented, same as dev kit | USB-A present; VBUS control unverified | slot present, no power-switch pin | IP101GRI present; no driver wired in |
| M5Stack Tab5 | documented-only | **no display backend** | pins confirmed by `apps/orcsdr-tab5` | via expander line, not driven | slot present | none |
| LilyGO T-Display-P4 V1.0 AMOLED + SX1262 | documented-only | **no RM69A10 backend**; a GT9895 (0x5D) probe is logged and refused | C6 SDIO pins match the dev kit; enable is XL9535 IO14, not driven | not claimed (PHY power is an expander line, VBUS unverified) | slot present; power is XL9535 IO15, not driven | IP101 present; reset is XL9535 IO5; no driver |

"Works" for the first row means what the earlier bring-up recorded in `docs/HOSYOND_7IN_DSI_P4.md` and the health
check this session (Home draw 12.9 ms, receiver streaming, audio flowing). The other rows have not been run as a
console, apart from the single WIFI6-DB observations named in that row.

## Sources and discrepancies

Waveshare Module-DEV-KIT: Waveshare wiki (DSI 2-lane connector for 5/7/8/10.1" screens, USB-A OTG with jumper,
C6 on SDIO, TF slot) plus bench use. The wiki shows the GPIO table as images, so pin numbers rest on the
schematic and on the bench, not on text.

Waveshare WIFI6-DB: Waveshare's wiki https://docs.waveshare.com/ESP32-P4-WIFI6-DB (read 2026-10-29). ESP32-P4NRW32X, 32 MB
NOR flash, 32 MB PSRAM in the package, **ESP32-C5** on SDIO (CLK18 CMD19 D0-D3 14-17, C5_CHIP_PU on GPIO54 "high to
enable, low to reset", GPIO6 wake), no Ethernet, microSD with power control on GPIO45 (low = on), ES8311 at 0x18, I2S on
GPIO9-13, amplifier enable GPIO53, BOOT key GPIO35, Type-C power/flash/debug plus a 4-pin USB OTG header, 2-lane DSI and
CSI connectors. The page gives no I2C pins, PCB revision or chip revision and names no ESP-Hosted C5 image.

Waveshare WIFI6-POE-ETH: the espp component readme (v1.3.7). It is a third-party summary of Waveshare's schematic
and names no board revision. Two boards in the Waveshare family share most pins (I2C 7/8, ES8311 on I2S
13/12/10/9/11 with amplifier enable 53, SDIO 18/19/14-17 with reset 54, SD 39-44), but the dev kit switches SD
power on GPIO45 and this board does not. Read the schematic PDF before trusting any of it. Which exact Waveshare
board the owner has still needs to be confirmed from its silkscreen.

M5Stack Tab5: Espressif `esp-bsp` `bsp/m5stack_tab5` at commit `f259a6ab` (2026-10-08): I2C 31/32, I2S 30/27/29/26/28,
SD 39-44 (CLK 43, CMD 44, D0-D3 39-42), backlight GPIO22, touch INT GPIO23, DSI 2 lanes at 1 Gbps, 720x1280.
Two expanders (PI4IOE5V6408) carry: expander 0 LCD enable bit 4, touch enable bit 5, speaker enable bit 1, camera
enable bit 6; expander 1 USB enable bit 3, Wi-Fi enable bit 0. The C6 SDIO pins (CLK12 CMD13 D0=11 D1=10 D2=9 D3=8)
and reset GPIO15 come from `apps/orcsdr-tab5/sdkconfig.defaults`, which runs on a real Tab5; the BSP header does
not list them. Expander address 0x44 is confirmed by `apps/orcsdr-tab5/ui/main.cpp`; 0x43 is the part's other
address and is not separately confirmed. The BSP selects the assembly at boot by probing the touch address: ILI9881C + GT911
or ST7123 + ST7123 touch at the commit I read, plus ST7121 per the research branch's newer pin (see below).

## Headless mode

With no recognised panel (or a failed display bring-up) the console no longer stops. It logs
`no usable display ... running headless`, keeps the M5GFX scaled display as an off-screen 800x480 sprite, and
starts everything else: serial interface, receiver, audio, Wi-Fi, SD and the UI task. `present_frame()` consumes
the dirty rows without converting them, so the UI keeps running and screenshots over serial (`FT8 SNAP`) still
work. Touch is not started. This is what a board with no screen attached runs.

## Research cross-check (branch `research/esp32-p4-board-catalog-20261028`, commit `0d6fa952`)

Read: HANDOFF.md, SOURCE_CONFLICTS.md, PINMAP_AND_PERIPHERAL_EVIDENCE.md. That pass ran no builds and no hardware
tests, so everything in it is documentary. Effects on these profiles:

- **Tab5 has three display paths**, ILI9881C+GT911, ST7123 and ST7121 (conflict C05). My profile was written from an
  older BSP commit that showed two. The Tab5 profile comment now says so; detect the assembly at runtime.
- **Waveshare WIFI6-DEV-KIT is a separate product** from the POE-ETH board I profiled, with PCB Rev1.1 and Rev1.2
  routing the USB host jumper to different ports (conflict C04: Rev1.1 HOST = ports 2-4, Rev1.2 HOST = port 1).
  Which exact Waveshare board is on COM29 is unconfirmed, so its PCB revision must be read from the silkscreen
  before its USB host path is trusted.
- **Module-DEV-KIT USB** is listed "not yet tested with RTL-SDR" in the research; this firmware did run an RTL-SDR
  on it. OrcNode's notes say its Type-A ports sit behind a CH334F hub and that the hub's port-power command has no
  electrical effect, so VBUS control stays a hardware matter.
- **ESP-Hosted C6 firmware version** is a P0 gap in the research (C08): matching SDIO wires do not prove a
  compatible coprocessor image. That is relevant to the COM8 Wi-Fi failure below.
- **LilyGO T-Display-P4** (pinned driver commit `a47cf73e`): ES8311 on I2S MCLK13 BCLK12 WS9 DOUT10 DIN11 (WS and
  DOUT are swapped relative to the Waveshare boards), two I2C buses (7/8 and 20/21), XL9535 expander at 0x20
  (screen reset IO2, touch reset IO3, touch int IO4, USB PHY power IO10, C6 enable IO14, SD power IO15, radio reset
  IO16, radio DIO1 IO17), SDIO 14-19 as on Waveshare, IP101 RMII pins as on Waveshare, HI8561 touch at 0x68, needs
  ESP-IDF >= 5.5.4. The expander is how most control lines are reached, so the profile type's expander fields matter.
  The AMOLED assembly (RM69A10 + GT9895) now has a documented-only profile, `lilygo-t-display-p4-amoled`, for the
  SX1262 unit. The TFT assembly (HI8561) is still not a profile. Neither panel has a display backend.
- No profile was enabled by default because of this research; the default is still the Module-DEV-KIT.

## Evidence from boards on the bench (read-only)

- **COM28** runs the OrcNode firmware as a live node (Wi-Fi connected, one RTL-SDR, Ethernet started, no link). It was
  only listened to. OrcNode's source (`F:\Ai\OrcNode`, `main/network_eth.cpp`, `sdkconfig.defaults`) uses Ethernet
  IP101 at PHY address 1 with MDC31 MDIO52 reset51, RMII clock-in 50, TX_EN49 TXD0 34 TXD1 35 CRS_DV28 RXD0 29 RXD1 30,
  and ESP-Hosted SDIO CLK18 CMD19 D0-D3 14-17 with reset 54. Its `PROJECT_TRUTH.md` marks Ethernet, SDIO, SD (GPIO
  43/44/39-42, LDO channel 4) and Wi-Fi hardware-verified on a Module-DEV-KIT (silicon v1.3, 16 MB flash). Those
  values are in the Waveshare profiles now, labelled as taken from OrcNode.
- **COM29** is an ESP32-P4 revision v3.2 with 32 MB flash (GigaDevice 0xC8/0x4019), MAC e8:f6:0a:eb:76:e7, running
  Espressif's `esp-brookesia` demo (built 2026-08-31, ESP-IDF v5.5.5; partitions: nvs 0x9000, factory 0x110000
  size 9 MB, storage 0xa10000 size 5 MB). It is a different silicon revision and flash size from the OrcNode board,
  so it is not the same board as the Module-DEV-KIT above. By elimination it is most likely the **ESP32-P4-WIFI6-DB**:
it matches the 32 MB flash, has no Ethernet, the Waveshare platform BSP in the stock demo, an ES8311 that answered at
0x18 on SDA7/SCL8, and the owner named a "p4 wifi6 db" board. Not confirmed: the module marking and PCB revision on
the board have not been read.

## Unsupported, by board

- **Tab5**: the console drives its panel through a TC358762 bridge (the Hosyond path). The Tab5 panel is native
  DSI with ILI9881C or ST7123 and needs its own display backend, a touch driver for GT911/ST7123, and expander
  control for USB power and Wi-Fi power. Until then the Tab5 runs its own firmware. Do not flash it with the
  console build.
- **WIFI6-POE-ETH**: no Ethernet driver in the app; USB-A VBUS control, DSI panel behaviour and C6 firmware version
  are unverified.
- **Every board**: GNSS, LoRa, IMU, RTC, camera and battery gauge are not used by the console.
- **LilyGO T-Display-P4 AMOLED + SX1262**: profile `lilygo-t-display-p4-amoled` is documented-only
  (`board_lilygo_t_display_p4_amoled.c`, overlay `sdkconfig.board-lilygo-tdisplay-p4-amoled`). Pins are the vendor
  header at `a47cf73e`: panel I2C 7/8, codec I2C 20/21, I2S WS9 DOUT10, SX1262 SPI SCLK2 MOSI3 MISO4 CS24 BUSY6
  at 10 MHz with reset/DIO1/antenna-switch on the XL9535. The 568x1232 RM69A10 timings are recorded in that file
  and are not programmed. A probe that sees GT9895 at 0x5D logs the match and stays headless. Expander outputs
  are not driven. The 4.05" TFT assembly is not this profile.

## Which boards are easiest

1. Waveshare Module-DEV-KIT: done.
2. Waveshare WIFI6-POE-ETH: same pins and same DSI connector family as the dev kit. First step is a bench
   boot with the Hosyond 7" panel: if the display, touch and audio come up, promote the profile for those groups.
3. M5Stack Tab5 and the LilyGO TFT assembly: each still needs a display backend. The AMOLED LilyGO profile
   exists and is documented-only; its backend is the same class of work (native DSI, not the TC358762 bridge).

## Add a board

1. Copy a file in `src/boards/`, fill every field (write `ORCSDR_PIN_NONE` for absent pins; never leave a pin at 0
   by omission), and cite a source for each group in the comment.
2. Add the extern to `src/boards/board_list.h`, the file to `CMakeLists.txt`, the entry to `k_boards[]` and the
   selector in `orcsdr_profiles.c`, and a `choice` entry in `Kconfig`.
3. Set status to `DOCUMENTED` or `SKELETON`, set `console_supported = false`, and write `unsupported`.
4. Add the board's ESP-Hosted pins to a build override and check that the boot log does not report a mismatch.

## Promoting a profile

Raise status to bench-verified only after, on that exact board and revision: the display, touch, powered USB host
with an RTL-SDR streaming, Wi-Fi over the board's own SDIO wiring, and the SD slot have each been exercised with
logs. The research branch defines the same gate as `FULL_CONSOLE_VERIFIED`; use its tracker once published.

## Build and test

Default board (Waveshare Module-DEV-KIT, nothing else to select):

```powershell
. C:\Espressif\frameworks\esp-idf-v5.5.4\export.ps1
Set-Location apps\orcsdr-p4-hosyond-console
idf.py -B build554-console -D IDF_TARGET=esp32p4 -D SDKCONFIG=build554-console/sdkconfig build
```

Another board profile uses an overlay file next to `sdkconfig.defaults` (`sdkconfig.board-wifi6-poe-eth`,
`sdkconfig.board-wifi6-db`, `sdkconfig.board-m5stack-tab5`, `sdkconfig.board-lilygo-tdisplay-p4-amoled`). Use a separate build directory so it does not disturb your default build:

```powershell
idf.py -B build-wifi6-poe-eth -D IDF_TARGET=esp32p4 -D SDKCONFIG=build-wifi6-poe-eth/sdkconfig `
  -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.board-wifi6-poe-eth" build
```

Flash only a board you have identified, with its own port. The Tab5 overlay is a compile check only.

### Silicon revision: two images

ESP-IDF 5.5 builds for ESP32-P4 silicon either **below v3.0** or **v3.0 and above**, never both in one image, and
`esptool` refuses a mismatched image before writing. `sdkconfig.defaults` targets v1.x (the Module-DEV-KIT bring-up
board is v1.3). For a v3.x chip (read it with `python -m esptool --port COMxx flash_id`: "revision v3.2") list the
overlay **last**:

```powershell
idf.py -B build-wifi6-poe-eth-v3 -D IDF_TARGET=esp32p4 -D SDKCONFIG=build-wifi6-poe-eth-v3/sdkconfig `
  -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.board-wifi6-poe-eth;sdkconfig.silicon-v3" build
```

Check `CONFIG_ESP32P4_REV_MIN_FULL=300` in the resulting sdkconfig. A release needs both images (or the web flasher
must read the chip revision and pick one). The v3 bootloader has only ~460 bytes free in its partition, so watch it.

After a flash to the dev kit on COM8, check:

```powershell
python tools/p4_health.py --port COM8
```

and read the boot log for `PROFILES: board: ... profile bench-verified`. A documented-only board prints
`this board's pins are not bench-verified` and its `unsupported here:` list.

## Open item: Wi-Fi does not start on two boards

Symptom on both the COM8 Module-DEV-KIT (v1.x silicon) and the COM29 board (v3.2 silicon): ESP-Hosted logs
`sdmmc_init_ocr: send_op_cond ... 0x107` / `sdmmc_card_init failed` on every attempt, so nothing answers on the
SDIO bus. The pins in the log are the profile's (CLK18 CMD19 D0-D3 14-17, reset 54). Headless COM28 (OrcNode) uses
the same pins and has Wi-Fi up, so the wiring map is right.

Tried, one variable each:
- Power-cycling COM8 (it had ~21 minutes of uptime when checked): no change.
- Reset polarity: our `sdkconfig.defaults` sets active-high (copied from the Tab5); upstream default and OrcNode are
  active-low. Built COM29 with active-low (`sdkconfig.board-wifi6-poe-eth`): **no change**, same timeouts. That
  overlay keeps active-low because it matches the working OrcNode and upstream, but it does not fix this by itself.

Not yet tried / unknown:
- **The coprocessor on COM29 is probably an ESP32-C5** (see above). That alone explains the failure: our embedded image
  is the ESP32-C6 build, and nothing on the C5 speaks ESP-Hosted unless a C5 slave image was flashed. The profile now
  carries `wifi_coprocessor`; with a non-C6 chip the firmware says so at boot and refuses the on-device coprocessor
  update (`RTL_WIFI C6_UPDATE` and the Settings button; the refusal is compiled in and auth-gated, not exercised on
  hardware). Espressif lists C5 as an esp-hosted slave target and recommends esp_hosted >= 2.4.2 and ESP-IDF >= 5.5, but
  I found no prebuilt C5 SDIO image and no confirmed P4+C5 working example; an Espressif FAQ item reports
  insufficient memory with Wi-Fi and BLE on a P4+C5. To get Wi-Fi: build the esp-hosted slave example for the C5
  target and flash it through the C5 UART pads (needs a USB-UART adapter and wiring at the board; BOOT and enable
  control are on the pads). Not done.
- **What the coprocessor is and what it runs** (COM8, and any board where the chip is not confirmed). The research catalog lists Espressif Function-EV variants with a C5
  (`espressif-function-ev-c5`, discovery pending). Our embedded image is the ESP32-C6 ESP-Hosted 3.0.6 build for the
  Tab5; a C5 needs its own image and an ESP32-C5 target. The COM29 stock demo (Waveshare platform BSP, no Wi-Fi
  strings) does not reveal it. Read the module marking on the board (ESP32-C6-MINI vs a C5 module).
- **C6 firmware state.** An ESP-Hosted slave image must be flashed on the C6; the SDK does not prove it is. The
  Waveshare boards expose the C6 UART (H7: TXD0, RXD0, IO9) for reflashing.
- SDIO clock (ours 10 MHz, OrcNode 40 MHz) and the reset strategy (ours "only if necessary", OrcNode default).
- The earlier contained C6 link-loss incident on COM8 is not root-caused.

## Change record

- Board profile type extended: vendor, source, status, unsupported text, C6 SDIO pins, USB host, expanders, Ethernet.
- Profiles split into one file per board; Kconfig board choice; boot-time profile log and SDIO pin cross-check.
- SD module: a board without an SD power-switch pin is no longer reported as having no slot.
- Added profiles: Waveshare WIFI6-POE-ETH (documented-only), M5Stack Tab5 (documented-only).
- Headless mode: no recognised panel no longer stops the console; the UI renders off-screen and everything else runs.
- Module-DEV-KIT profile corrected: Ethernet is present (OrcNode, IP101); the SD slot was never exercised here.
- Ethernet pins added to the profile type; Waveshare profiles carry the OrcNode-verified RMII map.
- Added `sdkconfig.silicon-v3` (second image for v3.x silicon); WIFI6 overlay sets active-low C6 reset.
- Added the Waveshare ESP32-P4-WIFI6-DB profile (ESP32-C5) and `wifi_coprocessor`; the C6 image and update are refused on any other chip.
- Added the LilyGO T-Display-P4 AMOLED + SX1262 profile (documented-only). The profile type gained the codec I2C
  bus, the onboard radio pins, and the expander lines that board uses for C6 enable, speaker power, SD power,
  screen/touch reset, USB PHY power and Ethernet reset. A GT9895 answer is logged and does not select a panel.
