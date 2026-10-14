# OrcSDR Waveshare 4B — screen-by-screen acceptance

**Normative: do NOT shrink or globally scale Tab5 1280x720 dashboard code.**
Each screen requires an independent inventory, defined geometry/hit targets,
native compilation, physical observation and touch test before it is verified.
ESP-IDF build success does not imply screen parity.

## Home v1 (IN DEVELOPMENT, NOT DEVICE VERIFIED)

Reference: apps/orcsdr-tab5/ui/home_dashboard.cpp and home_dashboard.hpp.
Existing Tab5 Home includes browser, direct frequency keypad, mode/filter
selection, gain/AGC/bias, audio, waterfall controls, stepped VFO, and more.

| Home behavior | Waveshare Home v1 |
|---|---|
| 720x720 OrcSDR header, receiver/USB state | Implemented, awaiting device |
| Live 256-bin spectrum, not calibrated dBm | Implemented, awaiting signal |
| IQ waterfall (scrolls only on actual IQ) | Implemented, awaiting device |
| Four frequency presets; +/- frequency step | Implemented, awaiting touch |
| 960k CU8 USB receiver using pinned esp-rtl-sdr | Implemented, awaiting OTG |
| WFM/NFM/AM/SSB audio | NOT PORTED — marked IQ-only |
| Browser, keypad, gain, filter, mode, bias-T | NOT PORTED |
| SD, recording, Wi-Fi, C6, OrcDial | NOT PORTED |
| Other dashboards | NOT PORTED |

### Home gates
- [x] Tab5 action/state inventory performed
- [x] Explicit 720x720 coordinate map and 48px+ hitboxes
- [x] Host geometry, tuning clamps, known-tone FFT test
- [ ] Native ESP-IDF 5.5.4 compile (see CI)
- [ ] Identify 4B physical board and silicon revision; flash provenance
- [ ] Physical display photo: fonts, color, clipping, waterfall, RGB565
- [ ] Touch: 50 taps including each preset, +/- and step; log frequency
- [ ] No dongle: status says NO SDR CONNECTED; never display fake RF
- [ ] SDR with externally powered USB-C OTG hub pending VBUS qualification
- [ ] Enumerate; 960 kS/s stream; known signal produces expected peak
- [ ] 5-minute stream while waterfall draws, >=95% effective samples, no sustained drops
- [ ] Release gate: none until user verifies board

Next screen is not started until Home has hardware acceptance evidence.
The website's chip-family auto-detection cannot tell Tab5 from Waveshare.
Waveshare remains blocked in hardware/boards.json.

For reproducibility, record actual ESP-IDF lockfile, firmware SHA and serial
log; keep logs next to scripts run on the bench.

### Local preflight, 2026-10-14 (not flashed)

- Source baseline: `ef117124dc0519f7403e51a0a7f094296b73914e`.
- Owner corrected the Waveshare programming port to **COM40**. COM39 is a
  different board and must not be used for this bring-up.
- COM40 esptool probe: P4 silicon v1.3, MAC `e8:f6:0a:e4:43:02`, flash
  manufacturer `c8`, device `4019`, detected capacity 32 MB. Probe left the
  device in bootloader; no flash writes were performed.
- PCB revision, socket labeling, factory firmware preservation, actual PSRAM
  initialization and USB Host power routing remain unverified.
- Existing CI artifact from run `38648107271` is **incompatible with this
  chip**: its saved configuration selects revisions 3.01–3.99 and 250 MHz
  PSRAM. Native CI compilation passed; the separate REA/Ghidra job failed.
- Added `sdkconfig.defaults.rev1_x` selecting revisions 1.00–1.99 and 200 MHz
  PSRAM, following Waveshare's separate silicon-profile guidance:
  https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-4B/blob/main/docs/board.md
- Home host test passed via Ubuntu g++ 13.3.0: geometry, tuning and FFT peak
  bin 160. Log qualification unit tests: 5 passed. These are host evidence,
  not physical display, touch or reception acceptance.
- Local SDK: ESP-IDF v5.5.4 at `735507283d5b2f9fb363a1901172dbd9e847945d`,
  with a pre-existing change in `esp_driver_sdio/src/sdio_slave.c` removing
  the 4092-byte upper bound in `sdio_slave_send_queue`; preserved unchanged.
  Python 3.14.3; RISC-V GCC 14.2.0 (`esp-14.2.0_20260121`).
- Logs are retained beside the commands: repository-root `preflight-com40-flash.log`,
  `home-host-tests.log`, `home-log-tests.log`; app-directory `idf-set-target.log`
  and `idf-build.log`. Downloaded CI evidence remains in `ci-build-evidence/`.
- One fresh local native build **passed** with the rev1.x overlay. Application
  size 810,688 bytes (`0xc5ec0`), 23% free in the 1 MiB factory partition;
  bootloader size `0x5d90`, 3% free. Application header confirms v1.0–v1.99.
  This proves compilation and image revision selection, not PSRAM operation.
- Generated `build-4b/flasher_args.json`: DIO, 80 MHz, 32 MB; bootloader
  `0x2000`, partition table `0x8000`, app `0x10000`. Partition table: NVS
  `0x9000`/24 KiB, PHY `0xf000`/4 KiB, factory `0x10000`/1 MiB. No C6 image.
  The partition-table SHA matches the CI artifact; bootloader/app differ as
  expected for a different silicon profile. Byte-for-byte reproduction is
  not claimed: local SDK is dirty, toolchains differ and the driver component
  hash in the local lock differs despite the same pinned Git revision. Other
  resolved dependency entries match the downloaded CI lock.
- Actual BSP 3.0.1 records upstream commit
  `69b3e7ba512e3676519196f5d91680445600a101`; its C source SHA-256 is
  `f5b7c99b6069c91676c164c23f30b41d410921b2aeafed07ac5791b543dc6f6f`.
  Display start selects rotation zero with triple-partial rendering.

Commands run (host commands from repo root; native commands from the app):

```text
g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic apps/orcsdr-waveshare-4b/tests/home_host.cpp -o home4b-host
./home4b-host
python -m unittest discover -s tools/hardware -p test_4b_home_log.py -v
idf.py -B build-4b -D SDKCONFIG=build-4b/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.rev1_x" set-target esp32p4
idf.py -B build-4b -D SDKCONFIG=build-4b/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.rev1_x" build
```

The g++ commands ran through WSL Ubuntu with absolute `/mnt/f/...` paths.
SHA-256 of the local images:

| Image | SHA-256 |
|---|---|
| Application | `b362b7db7cfd0450db132e2a5c6d95178846667e4c95ff07a4193bd81ad4eb3c` |
| Bootloader | `ef948b1ae6093c7342a06096d3749ee736fb491444ac4c9399a54c62b017a359` |
| Partition table | `7f00b6c042a89b15b0cac534f82ed988caf29278ff5700b0c511eb1b5bb7c820` |

Build input was baseline `ef117124` plus the rev1.x configuration overlay;
no application C++ source changed. Generated `image-info.log` and
`build-sha256.txt` are retained in the app directory. Final physical identity,
factory preservation and power-routing checks must complete before requesting
flash approval. Display, touch and five-minute RF gates remain open.

### Approved first flash and boot, 2026-10-14

- Owner photo confirms main-board silkscreen `ESP32-P4-WIFI6-Touch-LCD-4B`
  and module marking `Flash: 32MB`; no PCB revision marking is legible.
  Owner confirms PC on `USB TO UART`/COM40 and waives factory backup.
  Final flash explicitly approved after this identification.
- Re-probe matched P4 v1.3, MAC `e8:f6:0a:e4:43:02`, 32 MB flash.
  All three image hashes matched the preflight record above.
- Flashed the generated `flasher_args.json` files/settings using esptool
  4.12.0 at 460800 baud. All writes reported hash verification success.
  Used `--after no_reset`, then opened 115200-baud serial before a controlled
  UART RTS reset to capture the ROM boot onward. No full erase, NVS write,
  C6 write, or access to COM39 occurred in this flash operation.
- Captured 30 seconds of boot/runtime: P4 v1.3, **32 MB PSRAM at 200 MHz**,
  `SPI SRAM memory test OK`, expected project/version/ELF prefix `d1ce34905`,
  display initialized, GT911 found at `0x5D`, `HOME_4B screen initialized
  720x720 / IQ-only`, and `HOME_4B boot complete - C6 untouched`.
- USB device count remained zero. Physical touches generated frequency
  changes in 12,500 Hz increments/decrements and preset requests for
  96.1, 125 and 145.120 MHz. This is partial touch-path evidence; all-button
  coordinates, STEP cycling and the 50-tap acceptance are not yet verified.
- No panic/reboot observed during the capture. Warnings retained for bench
  correlation: LEDC GPIO26 usability/conflict, MIPI DSI frame-buffer-complete
  callback unavailable, I2C pull-ups advisory, gesture recognition disabled.
  Their presence alone does not establish a visual or touch failure.
- Logs retained in the app directory: `approved-com40-preflash.log`,
  `approved-com40-flash.log`, `approved-com40-boot.log`. Local bench script
  `approved_flash_capture.py` records the recipe/capture procedure.
  SHA-256: flash log
  `3dd3a9dad2db25b13703a3dced7487ba51458d097a1b50ca7ea2ca768c8d737e`;
  boot log `c676db1502b57cff6921ff7fe7c0bab95281e39fa40d1c0c1f676342cc250866`.
- Firmware write, PSRAM startup, Home initialization and partial physical
  touch events are verified. Visual display acceptance, full touch coverage,
  OTG power routing, real SDR reception and five-minute qualification remain
  open. No SDR connection requested until initial visual acceptance.
