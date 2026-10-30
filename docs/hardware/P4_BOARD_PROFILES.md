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
| Waveshare ESP32-P4-ETH (the COM29 board, identified by the owner 2026-10-29) | documented-only; I2C codec 0x18 and USB-host RTL-SDR observed on COM29 | headless runs, regression passed (see "COM29 regression") | **none: the board has no Wi-Fi or Bluetooth**; the console reports "no Wi-Fi radio" and does not start ESP-Hosted | 4-pin header; an RTL-SDR streams at 2.4 MS/s, 0 drops | slot present, power pin unknown, no card tested | 100M RJ45; PHY and pins from OrcNode/third party, no driver wired in |
| Waveshare ESP32-P4-WIFI6-DB (the COM31 board, owner-identified 2026-10-30) | documented; SDIO link to the C5, codec 0x18 and a USB-host RTL-SDR observed on COM31 | headless runs; regression run (see "COM31") | **ESP32-C5, link up**: stock slave is ESP-Hosted 2.12.9, host is 3.0.6, major mismatch, Wi-Fi down; C6 image never offered | 4-pin header; Blog V4 streams at 2.4 MS/s, 0 drops | slot present (power GPIO45, low = on), mount timed out with no card | none |
| Waveshare ESP32-P4-WIFI6-POE-ETH | documented-only | not run | pins documented, same as dev kit | USB-A present; VBUS control unverified | slot present, no power-switch pin | IP101GRI present; no driver wired in |
| M5Stack Tab5 | documented-only | **no display backend** | pins confirmed by `apps/orcsdr-tab5` | via expander line, not driven | slot present | none |

"Works" for the first row means what the earlier bring-up recorded in `docs/HOSYOND_7IN_DSI_P4.md` and the health
check this session (Home draw 12.9 ms, receiver streaming, audio flowing). Nothing in the other two rows has run.

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
  COM29 turned out to be the ESP32-P4-ETH (owner, 2026-10-29), a different product again; the POE-ETH and DEV-KIT
  USB-host jumper findings do not apply to it.
- **Module-DEV-KIT USB** is listed "not yet tested with RTL-SDR" in the research; this firmware did run an RTL-SDR
  on it. OrcNode's notes say its Type-A ports sit behind a CH334F hub and that the hub's port-power command has no
  electrical effect, so VBUS control stays a hardware matter.
- **ESP-Hosted C6 firmware version** is a P0 gap in the research (C08): matching SDIO wires do not prove a
  compatible coprocessor image. That is relevant to the COM29 Wi-Fi failure below.
- **LilyGO T-Display-P4** (pinned driver commit `a47cf73e`): ES8311 on I2S MCLK13 BCLK12 WS9 DOUT10 DIN11 (WS and
  DOUT are swapped relative to the Waveshare boards), two I2C buses (7/8 and 20/21), XL9535 expander at 0x20
  (screen reset IO2, touch reset IO3, touch int IO4, USB PHY power IO10, C6 enable IO14, SD power IO15, radio reset
  IO16, radio DIO1 IO17), SDIO 14-19 as on Waveshare, IP101 RMII pins as on Waveshare, HI8561 touch at 0x68, needs
  ESP-IDF >= 5.5.4. The expander is how most control lines are reached, so the profile type's expander fields matter.
  No LilyGO profile is added yet: the AMOLED variant is a different panel (RM69A10 + GT9895) and the board is not here.
- No profile was enabled by default because of this research; the default is still the Module-DEV-KIT.

## Evidence from boards on the bench (read-only)

- **COM28** runs the OrcSDR Web firmware of the other session (repo `OrcSDR-Headless`, branch
  `feat/m5gfx-web-mirror`), not OrcNode firmware (owner's statement, 2026-10-29). It was seen with Wi-Fi connected, one
  RTL-SDR, Ethernet started, no link, and only listened to. That branch grew out of OrcNode, and its
  `main/network_eth.cpp` and `sdkconfig.defaults` carry the same Ethernet and SDIO pins as the OrcNode source cited
  next (compared 2026-10-29). OrcNode's source (`F:\Ai\OrcNode`, `main/network_eth.cpp`, `sdkconfig.defaults`) uses Ethernet
  IP101 at PHY address 1 with MDC31 MDIO52 reset51, RMII clock-in 50, TX_EN49 TXD0 34 TXD1 35 CRS_DV28 RXD0 29 RXD1 30,
  and ESP-Hosted SDIO CLK18 CMD19 D0-D3 14-17 with reset 54. Its `PROJECT_TRUTH.md` marks Ethernet, SDIO, SD (GPIO
  43/44/39-42, LDO channel 4) and Wi-Fi hardware-verified on a Module-DEV-KIT (silicon v1.3, 16 MB flash). Those
  values are in the Waveshare profiles now, labelled as taken from OrcNode.
- **COM29** is an ESP32-P4 revision v3.2 with 32 MB flash (GigaDevice 0xC8/0x4019), MAC e8:f6:0a:eb:76:e7, running
  Espressif's `esp-brookesia` demo (built 2026-08-31, ESP-IDF v5.5.5; partitions: nvs 0x9000, factory 0x110000
  size 9 MB, storage 0xa10000 size 5 MB). It is a different silicon revision and flash size from the OrcNode board,
  so it is not the same board as the Module-DEV-KIT above. **The owner identified it as the Waveshare ESP32-P4-ETH**
(2026-10-29), and Waveshare's page (https://docs.waveshare.com/ESP32-P4-ETH) matches: 32 MB flash, 32 MB PSRAM, a 100M
Ethernet port, a TF slot, a USB 2.0 header, and no Wi-Fi or Bluetooth. An earlier note here guessed WIFI6-DB with an
ESP32-C5 by elimination; that was wrong (it assumed the board had a radio). PCB revision not read.
- **COM31** is a second ESP32-P4 v3.2, 32 MB flash (GigaDevice c8/4019), MAC 80:f1:b2:d7:bc:55, found boot-looping
  (ROM `SW_CPU_RESET` repeating, no application output) with a stock demo layout (nvsfactory 0x9000, nvs 0x3b000, otadata
  0x10d000, phy_init 0x10f000, factory 0x110000 size 10 MB, model 0xb10000, storage 0xb90000 size 6 MB). **The owner
  identified it as the Waveshare ESP32-P4-WIFI6-DB with an ESP32-C5 (2026-10-30).** Stock flash 0 to 0x1190000 backed up to `F:\Ai\_p4_stock_backups\com31_20261029\com31_stock_0-0x1190000.bin`
  (18,415,616 bytes, SHA-256 `d35b9225c70452277999278bbaeb2513f35e6585e1f73bbf169cd5830c7848d5`, bootloader, partition
  table and app magic checked). On 2026-10-30 `build-multi1-v3` (profile `waveshare-esp32-p4-wifi6-db`, silicon v3, one
  receiver) was flashed to it (hash verified); the stock firmware remains only in the backup.

## COM31 regression (ESP32-P4-WIFI6-DB with ESP32-C5, single receiver, 2026-10-30)

- Boot: profile line, C5 warning, `running headless at 800x480` (only 0x18 answers on the DSI I2C bus), ES8311 codec opened, `boot complete`.
- Radio: Blog V4 on the USB header, `RTL_RX STATUS` receiver 0 streaming 2.4 MS/s (2,398,728), drops 0; radio task 44.8 % of core 1 (COM29: 44.7 %),
  IDLE1 53.3 %, audio 1.8 %. 40 s of log: no drops, no reset.
- Memory with the Wi-Fi attempt made, no card: internal free 163,535 B (largest 90,112), DMA free 124,507 B, PSRAM free 31,882,680 B.
- SD: `SDMMC Slot0 mount failed: ESP_ERR_TIMEOUT` (no card inserted; not a fault by itself).
- **Wi-Fi coprocessor, new fact:** the ESP-Hosted SDIO link comes up (`Card init success`, `slave chip id: 0x17 (esp32c5)`) and the C5
  runs a stock ESP-Hosted slave, **firmware 2.12.9**. Host here is 3.0.6; the host logs "major version mismatch ... OTA coprocessor from
  host" and the console correctly refuses to push its C6 image (`RTL_WIFI STATUS` stage `hosted_connect`, code -5). So the SDIO pins,
  active-low reset on GPIO54 and the 10 MHz clock are right for this board; only the slave firmware version is wrong.
- Options, none done: (1) host-driven OTA of an ESP-Hosted 3.0.x C5 slave image over the existing link (needs a C5 image embedded and the
  OTA path allowed for C5; a bad image could leave the C5 unreachable, recoverable only through its UART pads); (2) flash the C5 through
  its UART pads; (3) a 2.x-compatible host (rejected: far from the pinned 3.0.6 everywhere else). The built image is
  `F:\Ai\OrcSDR\.orcsdr-cache\hosted-c5\build\eh_cp_wifi_scan.bin` (3.0.6); it has not run on any C5.


## Unsupported, by board

- **Tab5**: the console drives its panel through a TC358762 bridge (the Hosyond path). The Tab5 panel is native
  DSI with ILI9881C or ST7123 and needs its own display backend, a touch driver for GT911/ST7123, and expander
  control for USB power and Wi-Fi power. Until then the Tab5 runs its own firmware. Do not flash it with the
  console build.
- **WIFI6-POE-ETH**: no Ethernet driver in the app; USB-A VBUS control, DSI panel behaviour and C6 firmware version
  are unverified.
- **Every board**: GNSS, LoRa, IMU, RTC, camera and battery gauge are not used by the console.
- **LilyGO T-Display-P4** (arriving): no profile yet. Facts gathered, all unverified: ESP32-P4 with 16 MB flash and
  32 MB PSRAM; two screens (4.05" TFT 540x1168 via HI8561, 4.1" AMOLED 568x1232 via RM69A10 with GT9895 touch);
  ESP32-C6-MINI-1U; SX1262 or LR2021 LoRa on SPI; ES8311 audio; XL9535 expander (also switches the LoRa antenna
  path: 1 = internal, 0 = external MMCX1); microSD; Ethernet; L76K GNSS; PCF8563 RTC; ICM20948 IMU; BQ27220 gauge.
  There are two hardware folders (`v1`, `v2`) in the vendor driver repo; the pin header could not be read this
  session (the repo is a submodule and the fetch failed). It needs two display backends (HI8561, RM69A10) like the Tab5.

## Which boards are easiest

1. Waveshare Module-DEV-KIT: done.
2. Waveshare WIFI6-POE-ETH: same pins and same DSI connector family as the dev kit. First step is a bench
   boot with the Hosyond 7" panel: if the display, touch and audio come up, promote the profile for those groups.
3. M5Stack Tab5 and LilyGO T-Display-P4: each needs new display backends, so they come after the above.

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
`sdkconfig.board-wifi6-db`, `sdkconfig.board-m5stack-tab5`). Use a separate build directory so it does not disturb your default build:

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

## COM29 regression (ESP32-P4-ETH, single receiver, 2026-10-29)

Image: `build-p4eth-v3` (profile `waveshare-esp32-p4-eth`, silicon v3, `CONFIG_ORCSDR_MAX_RECEIVERS=1`), flashed to COM29 (hash
verified), a Blog V4L RTL-SDR on the USB header. Results:
- `RTL_RX STATUS`: receiver 0 streaming, 2,400,000 S/s (effective 2,399,9xx), drops 0, `usb_path="root"`, device "Blog V4L".
- `RTL_MEM STATUS` (headless, no Wi-Fi): internal free 190,575 B (largest 106,496), DMA free 151,547 B, PSRAM free 31,914,028 B.
- `RTL_CPU`: radio task 44.7 % of core 1 at 2.4 MS/s, IDLE1 53.4 %, audio 1.8 %.
- `RTL_WIFI STATUS`: `started=0 stage=no Wi-Fi radio`, no ESP-Hosted retries.
- Sink add/remove path: RF_LAB, ACARS and LORA each opened and closed 12 times via `RTL_UI OPEN` (72 screen changes, each with
  a sample-rate change and receiver restart): no crash, panic or watchdog output; the receiver was streaming again afterwards.
- `tools/p4_health.py --port COM29`: PASS (draw 10.7 ms, frame period 50 ms, IDLE1 53.3 %, audio 590 blocks in 2 s, 0 dropped).
- Not done: a before/after CPU comparison on the same board (no pre-change CPU figure was recorded), the two-receiver path
  (needs a second dongle), hot-plug, a long soak.

## Wi-Fi on COM29: resolved, the board has no radio

The text below is the investigation as it stood before the board was identified. Its conclusion (a C5 coprocessor) was wrong:
the ESP32-P4-ETH has no Wi-Fi hardware, so ESP-Hosted can never answer. The SDIO timeouts were nothing on the bus.
(COM8's Wi-Fi is a different board, a Module-DEV-KIT with a C6, and works.)

**COM8 update (2026-10-29): COM8 Wi-Fi works.** The owner saw it connected on the Connectivity Settings screen at
192.168.1.198, and `RTL_WIFI STATUS` on the serial port confirmed it (`started=1 connected=1 ssid=hardcorewifi
ip=192.168.1.198 c6=3.0.6`, uptime about 10.5 h). The earlier COM8 failure is therefore not a standing fault of the
Module-DEV-KIT profile, and what cleared it is unknown (a power cycle alone had no effect when tried; see below), so the
earlier link-loss/start failures on COM8 are not root-caused. Free memory on COM8 at that time, Wi-Fi up, no dongle
attached: internal 166,259 B free, largest block 94,208 B; PSRAM 30,305,920 B free.

Symptom that remains on the COM29 board (v3.2 silicon): ESP-Hosted logs
`sdmmc_init_ocr: send_op_cond ... 0x107` / `sdmmc_card_init failed` on every attempt, so nothing answers on the
SDIO bus. The pins in the log are the profile's (CLK18 CMD19 D0-D3 14-17, reset 54). COM28 uses
the same pins and has Wi-Fi up, so the wiring map is right.

Tried, one variable each (the first two were done while COM8 was failing; COM8 now works):
- Power-cycling COM8 (it had ~21 minutes of uptime when checked): no change.
- Reset polarity: our `sdkconfig.defaults` sets active-high (copied from the Tab5); upstream default and OrcNode are
  active-low. Built COM29 with active-low (`sdkconfig.board-wifi6-poe-eth`): **no change**, same timeouts. That
  overlay keeps active-low because it matches the working OrcNode and upstream, but it does not fix this by itself.

Not yet tried / unknown:
- **(Superseded: COM29 has no coprocessor at all, see above. The C5 analysis stays valid for a real WIFI6-DB.)** The old
  guess: the coprocessor on COM29 is probably an ESP32-C5. That alone explains the failure: our embedded image
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
- The earlier contained C6 link-loss incident on COM8 is not root-caused (COM8 Wi-Fi works now).
- A built ESP-Hosted ESP32-C5 slave image exists (not flashed; the COM31 WIFI6-DB runs stock slave 2.12.9): `F:\Ai\OrcSDR\.orcsdr-cache\hosted-c5\build\eh_cp_wifi_scan.bin`,
  SHA-256 `d5343933246d61df0a8e67da077c2ac20ed611e013132ceea56efe6c148b496d`, upstream ESP-Hosted 3.0.6
  `examples/wifi/scan/cp`, ESP-IDF 5.5.4, SDIO slave pins CMD10 CLK9 D0 8 D1 7 D2 14 D3 13 (build defaults; not yet
  checked against the board schematic).

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
- Added the Waveshare ESP32-P4-ETH profile (COM29, no Wi-Fi radio) and `sdkconfig.board-p4-eth`; the console no longer starts ESP-Hosted on a board whose profile has no coprocessor. Retracted the guess that COM29 is a WIFI6-DB with a C5.
- Added COM31 (second v3.2 unit, model unidentified) and its stock-flash backup record.
- COM31 identified as a WIFI6-DB: SDIO link to its ESP32-C5 works, stock slave 2.12.9 vs host 3.0.6 (Wi-Fi blocked by version mismatch); single-receiver regression recorded.
