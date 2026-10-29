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
| Waveshare ESP32-P4-Module-DEV-KIT + Hosyond 7" 800x480 | bench-verified | works | pins match; see "Open item" | works (jumper selects host) | power switch GPIO45; no card inserted on the bench | none on board |
| Waveshare ESP32-P4-WIFI6-POE-ETH | documented-only | not run | pins documented, same as dev kit | USB-A present; VBUS control unverified | slot present, no power-switch pin | IP101GRI present; no driver wired in |
| M5Stack Tab5 | documented-only | **no display backend** | pins confirmed by `apps/orcsdr-tab5` | via expander line, not driven | slot present | none |

"Works" for the first row means what the earlier bring-up recorded in `docs/HOSYOND_7IN_DSI_P4.md` and the health
check this session (Home draw 12.9 ms, receiver streaming, audio flowing). Nothing in the other two rows has run.

## Sources and discrepancies

Waveshare Module-DEV-KIT: Waveshare wiki (DSI 2-lane connector for 5/7/8/10.1" screens, USB-A OTG with jumper,
C6 on SDIO, TF slot) plus bench use. The wiki shows the GPIO table as images, so pin numbers rest on the
schematic and on the bench, not on text.

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
address and is not separately confirmed. The BSP selects between two board versions at boot by probing the touch
address: ILI9881C + GT911, or ST7123 + ST7123 touch.

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
`sdkconfig.board-m5stack-tab5`). Use a separate build directory so it does not disturb your default build:

```powershell
idf.py -B build-wifi6-poe-eth -D IDF_TARGET=esp32p4 -D SDKCONFIG=build-wifi6-poe-eth/sdkconfig `
  -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.board-wifi6-poe-eth" build
```

Flash only a board you have identified, with its own port. The Tab5 overlay is a compile check only.

After a flash to the dev kit on COM8, check:

```powershell
python tools/p4_health.py --port COM8
```

and read the boot log for `PROFILES: board: ... profile bench-verified`. A documented-only board prints
`this board's pins are not bench-verified` and its `unsupported here:` list.

## Open item found while testing this change

After flashing this branch, Wi-Fi did not start on COM8: `eh_host_port_sdio: sdmmc_card_init failed`
(`send_op_cond ... 0x107`, timeout) on every attempt, so the ESP32-C6 is not answering on SDIO. The pins in the log
are the profile's pins (CLK18 CMD19 D0-D3 14-17, reset 54). This change does not touch Wi-Fi code, and internal
RAM was healthy (144 KB free), but it has not been proven innocent: the previous firmware was not re-run for
comparison. The earlier C6 link-loss incident (contained, not root-caused) is the other candidate. A full power
cycle of the board is the first test.

## Change record

- Board profile type extended: vendor, source, status, unsupported text, C6 SDIO pins, USB host, expanders, Ethernet.
- Profiles split into one file per board; Kconfig board choice; boot-time profile log and SDIO pin cross-check.
- SD module: a board without an SD power-switch pin is no longer reported as having no slot.
- Added profiles: Waveshare WIFI6-POE-ETH (documented-only), M5Stack Tab5 (documented-only).
