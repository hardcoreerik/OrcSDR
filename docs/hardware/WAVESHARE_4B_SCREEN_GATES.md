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

### Home feedback and USB hub preparation, 2026-10-14

- Owner confirms the device booted but rejects this Home UI as an acceptable
  OrcSDR design; owner explicitly deferred UI changes. Home UI acceptance is
  **not passed**. Hardware USB investigation continues at the owner's request.
- During a direct dongle connection, serial continued reporting
  `device list count=0`; no receiver or IQ stream was observed. Adapter/data
  path, dongle power and OTG VBUS remain unverified. The owner disconnected it.
  See app-directory `dongle-connect-com40.log`.
- Found `CONFIG_USB_HOST_HUBS_SUPPORTED` disabled in the flashed image.
  This does not explain failure of the direct connection, but prevents the
  intended externally powered hub test. Enabled it in Waveshare app defaults
  at the owner's request; the esp-rtl-sdr driver remains the sole USB owner.
- New build directory `build-4b-hub` preserves the original flashed images.
  Generated config enables external hubs, including IDF's default multi-level
  support, while retaining rev1.00–1.99 and 200 MHz PSRAM. No UI/C6 change.
- One native ESP-IDF 5.5.4 build passed (exit 0), app 830,096 bytes,
  21% free in its 1 MiB partition; image header confirms revisions 1.0–1.99
  and valid image hash. `git diff --check` passed. No host tests repeated for
  this configuration-only change. No flash or physical hub test performed.
  Build input: `77102cf4` plus the hub-default change, same SDK/toolchain as
  the first build; app version records `v0.4.0-beta.1-16-g77102cf4-dirt`.
- Exact command, from the app directory in the exported IDF environment:
  `idf.py -B build-4b-hub -D IDF_TARGET=esp32p4 -D SDKCONFIG=build-4b-hub/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.rev1_x" build`.
  Evidence: `idf-build-hub.log`, `image-info-hub.log`, `build-hub-sha256.txt`.
- SHA-256: application
  `61dd15a9371c8ec983c0479880e6b193f7bad2f6d988e58be8f3744e655a12ef`;
  bootloader `6b880170daa8a536abe5db5a8813a3e062b051b525c94d2666fda0410659d096`;
  partition table unchanged at
  `7f00b6c042a89b15b0cac534f82ed988caf29278ff5700b0c511eb1b5bb7c820`.
  Generated `build-4b-hub/flasher_args.json` retains the three original
  offsets, DIO/80 MHz/32 MB settings, and no C6/NVS image. Final flash approval
  is required before installing this hub-enabled image on COM40.

### Approved hub-enabled flash, 2026-10-14

- Owner explicitly approved installation. COM40 re-probe matched P4 v1.3,
  MAC `e8:f6:0a:e4:43:02`, flash 32 MB. All three new image hashes matched
  the hub-build record above. Flashed `build-4b-hub/flasher_args.json` through
  esptool 4.12.0 at 460800 baud; each image reported hash verification success.
- Used the same serial-before-reset capture procedure, preserving the first
  flash logs. Logs: `hub-com40-preflash.log`, `hub-com40-flash.log`,
  `hub-com40-boot.log`; local procedure script `hub_flash_capture.py`.
- Boot confirms app version `v0.4.0-beta.1-16-g77102cf4-dirt`, ELF prefix
  `5b9f6d4ea`, PSRAM 200 MHz and memory test OK, GT911 at `0x5D`, and
  `HOME_4B boot complete - C6 untouched`. No C6/NVS image or full erase.
- Hub-enabled image installation and Home startup verified. Owner invited
  to connect powered hub plus dongle while the serial capture runs. Hub and
  receiver enumeration, actual IQ and sustained RF remain separate open gates.

### Owner video: first visible IQ streaming, 2026-10-14

- Owner reports "we got it up" and supplies a 4.3648-second video plus photo.
  Video frames at 1.0 and 3.2 seconds show `RECEIVING IQ | 960 kS/s | drops 0`
  at 96.100000 MHz, changing relative spectrum and waterfall data. This is
  visual evidence of IQ streaming and Home rendering on physical hardware;
  the displayed rate/counter are not a serial-measured five-minute result.
- Photo shows an RTL-SDR Blog V4 and an inline meter displaying approximately
  4.184 V, 0.249 A, 1.040 W. Power margin and actual hub/adapter topology remain
  to be established; antenna and band suitability are not yet recorded.
- Serial capture failed at approximately 73.5 seconds uptime with Windows
  `GetOverlappedResult failed` / access denied. COM40 is absent from the
  subsequent port inventory, so receiver enumeration/start markers were not
  captured. Do not classify this PC serial-link loss as an MCU crash.
- Video `f1019aa9-73d3-4573-88d3-72429e24c8e6.mp4` SHA-256:
  `24f49f28036e147de8cd05786f59487924cf7088c297e6ce34659389e86d0c78`.
  Photo `codex-clipboard-ac1e6ef4-9edd-4c52-8eb8-6708ae195992.jpg` SHA-256:
  `bf6aa9f09f3975a924e6cf4dacddbef8419a1b46ed3982b782258c6ffa4ed4f1`.
  Originals remain at owner-supplied local paths; not committed as binaries.
- Open: known-signal peak/tuning response (a center peak alone is not proof),
  exact USB device/profile logs, physical topology, antenna, power routing,
  full touch coverage, and five-minute >=95% stream qualification. Home UI
  design remains rejected/deferred per owner; no redesign performed.

### Reconnected serial: five-minute stream measurement, 2026-10-14

- Owner restored COM40. Opened 115200 baud with DTR/RTS false and no reset.
  Serial confirms Home IQ at 96,100,000 Hz with increasing consumed bytes.
- Saved app-directory `com40-iq-stability.log`: 412 IQ rows spanning
  **309.18 seconds**, SPS range **960,009–960,091**, drops **0 throughout**,
  maximum telemetry gap 780 ms, consumed counter monotonic. Counter delta
  independently gives 960,052.16 complex samples/second (CU8, two bytes/IQ).
  These measurements meet the duration/rate/drop thresholds for the observed
  interval; no stream-ended, panic or starvation messages were captured.
- Ran `python ../../tools/hardware/check_4b_home_log.py --log com40-iq-stability.log`
  from the app directory. Strict checker **rejects** this reconnect-only log:
  `No RF START marker (no proven stream)`. Startup/enumeration occurred before
  capture. No synthetic marker added and no full qualification PASS claimed.
- Log SHA-256:
  `de071c982d9011b4c28f776ec2008214ffa2f2535c6b902eebe10e312b0eb24f`.
- Owner identifies working setup as V4 powered through a **power passthrough
  adapter**, with a **one-foot telescopic monopole**. Do not call this proof
  of external USB hub enumeration. Reception evidence is bounded to this
  setup at 96.1 MHz; antenna placement, ground plane and a known-station
  tuning/peak check remain unrecorded. No audio demodulation is implemented.
- Next qualification needs a complete enumeration/RF START plus >=300-second
  log and a known-signal tuning check. UI design acceptance remains deferred.

### Home v3 local preflight, 2026-10-14

- Isolated checkout of expected source
  `fda39842a004414fe8da03dac55d067c8fcb725a`, including the earlier evidence
  commits. Existing checkouts, original images and serial logs preserved.
- Scoped review covered the v3 LVGL renderer/header/drawer model, main-task
  glue, CMake, source badge, source waterfall palettes and Home references.
  Corrected STEP title overlap, retained six fractional MHz digits, corrected
  the boot version label and added STEP/palette verification logs. No driver,
  acquisition, Tab5 application or C6 changes.
- Original badge copies match SHA-256
  `638f26c0242ebc5eda5cb41b987a08d7627ea77a321173150b25ad07aa1d359d`.
  Shared Tab5 `waterfall_style.cpp` is compiled directly, Home starts CLASSIC.
  CMake embeds the fixed-symbol 21,632-byte badge. Physical RGB565 colors and
  owner visual acceptance remain unverified for v3.
- COM40 CH343 serial `5B91033266` re-probe: P4 v1.3,
  MAC `e8:f6:0a:e4:43:02`, 32 MB flash. Read-only esptool flash identification
  followed by UART hard reset; no firmware write. `preflight-v3-com40.log`
  saved at checkout root. COM39 not accessed.
- Host tests passed: `home_host` (known FFT tone at bin 160),
  `home_v2_layout_host`, `home_v3_drawer_host`; five log-checker unit tests
  passed. Each C++ test compiled via WSL Ubuntu g++ with
  `-std=c++17 -O2 -Wall -Wextra -Werror -pedantic -Iapps/orcsdr-waveshare-4b/main`
  and its `apps/orcsdr-waveshare-4b/tests/<test>.cpp` source, then executed.
  Per-test `*-build.log`/`*-test.log` and `log-checker-tests.log` retained at
  checkout root. Python command:
  `python -m unittest discover -s tools/hardware -p test_4b_home_log.py -v`.
- Build environment remains ESP-IDF 5.5.4 at
  `735507283d5b2f9fb363a1901172dbd9e847945d`, with the existing one-line
  SDIO-slave bounds-check modification preserved; Python 3.14.3, RISC-V GCC
  14.2.0 (`esp-14.2.0_20260121`). Dependency lock matches the previous working
  local lock; driver stays pinned to `31a159df4f006a837d5041029bc6d1bc63520234`.
- Generated config: silicon 1.00–1.99, PSRAM 200 MHz, flash 32 MB,
  external hubs enabled. No rev3.x CI image will be used on this board.
- No Home v3 physical display/photo, 50-toggle drawer test, palette/tuning
  touch validation, full RF-start capture or RF stability acceptance yet.
  The earlier 309.18-second Home v1 baseline does not qualify this renderer.
- One fresh native build **passed**, exit 0, using the README's exact
  `build-4b-home-v3`/rev1.x command. App 857,376 bytes (`0xd1520`), 18% free
  in 1 MiB factory partition; bootloader 23,952 bytes. Both image headers
  report silicon v1.0–v1.99 with valid checksum and validation hashes.
- ELF symbols confirm embedded badge start `0x400b69fc`, end `0x400bbe7c`
  (21,632 bytes), plus shared `waterfall_style::color565` and `next_palette`.
- Build input is `fda39842` plus the reviewed local C++ fixes; root
  `home-v3-source.patch` SHA-256 is
  `5952c5b12d4637b213eee1e49be4d9fe9d59fee108ea96001f0bf669bf0486b9`.
  App version `v0.4.0-beta.1-32-gfda39842-dirt`, ELF SHA-256
  `820278292c1c530928fefd7d1402b87e7f91f751d9ce023637be931c175ab656`.
  This is a local patched-source build, not a byte-identical CI reproduction.
- Image SHA-256:

  | Image | SHA-256 |
  |---|---|
  | App | `d4ec011e393c7d09c7a426807b8d1d16f87995032e257fb0e507efb761ba7403` |
  | Bootloader | `e18f9c479933cfebd5f922946c211a1fae509c0d9d399fce6a133babc8bfeafc` |
  | Partition table | `7f00b6c042a89b15b0cac534f82ed988caf29278ff5700b0c511eb1b5bb7c820` |

- Generated `flasher_args.json`: bootloader `0x2000`, partition table
  `0x8000`, app `0x10000`, DIO/80 MHz/32 MB. Partition hash matches the
  earlier working image. No C6/NVS image or full erase in the recipe.
  App-directory evidence: `idf-build-home-v3.log`, `home-v3-image-info.log`,
  `home-v3-bootloader-info.log`, `home-v3-sha256.txt`, `dependencies.lock`.
- Recommendation at this gate: **Home not yet verified**. Build and host
  tests passed; final flash approval, v3 photographs, original-color visual
  review, 50 drawer interactions and complete five-minute RF capture remain
  pending. Original firmware/power setup preserved. No new device write yet.

### Home v3 approved flash and receiver startup, 2026-10-14

- Owner explicitly approved COM40. Pre-write re-probe matched P4 v1.3,
  MAC `e8:f6:0a:e4:43:02`, 32 MB flash. Rechecked all three image SHA-256
  values against the v3 build record above. Source fixes/evidence commit:
  `e362266cbb5a848533835d900c3e4f5e8db45e28` (build metadata retains its
  `fda39842` plus reviewed-patch identity, as documented above).
- Used generated `build-4b-home-v3/flasher_args.json` via esptool 4.12.0 at
  460800 baud; all three writes verified their hashes. No full erase, C6
  image, NVS image, COM39 access or power-fixture change. Local bench script
  `flash_home_v3_capture.py` checks the approved hashes before writing.
- Opened UART at 115200 baud before controlled RTS reset and retained the
  complete ROM boot/receiver sequence in app-directory `home-v3-com40.log`.
  Flash and preflash logs: `home-v3-flash-com40.log`, `home-v3-preflash-com40.log`.
- Boot confirms PSRAM 200 MHz, memory test OK, ELF prefix `820278292`,
  GT911 at `0x5D`, v3 original-badge/palette renderer with 668px plot and
  `HOME_4B boot complete - C6 untouched` at 1735 ms uptime.
- USB diagnostics advance to `raw_devices=1 accepted_sdr=1`; actual detected
  profile is **`blog_v4l_r828s`**. Driver records 960000 SPS readback and
  6x16384 URBs. `HOME_4B RF START hz=96100000 rate=960000` captured at
  4875 ms. Unlike the earlier reconnect capture, startup markers are present.
- Physical photos, owner visual review, 50 drawer interactions, enabled
  button/palette tests and absence of navigation-induced degradation remain
  independent acceptance requirements. Driver logs alone do not qualify UI.
- Complete capture now covers **310.11 seconds**, **413 IQ rows** at
  96.1 MHz; post-warmup range **959,604–960,169 SPS**, drops **zero throughout**,
  maximum telemetry gap 780 ms, consumed counter monotonic. Byte-counter
  delta gives 960,158.09 complex samples/second. Previous 309.18-second
  zero-drop baseline is preserved for stationary Home v3 streaming under the
  same owner-established power-passthrough/one-foot-monopole setup.
- Unmodified strict command, from app directory:
  `python ../../tools/hardware/check_4b_home_log.py --log home-v3-com40.log`.
  **Exit 0**: `IQ PASS: 310.1s; min SPS 959604; added drops 0. Screen and touch remain UNVERIFIED.`
  `home-v3-strict-check.log` and `home-v3-measurement.json` retained beside log.
- Complete serial-log SHA-256:
  `44f78fce8daaff43684b90ff34b5d830b3a3f73a58518693653f760e10ae6bf8`.
- No NAV/TUNE/STEP/PALETTE events occurred during the timed capture. Do not
  claim a 50-interaction drawer test, palette switching, physical hitbox
  acceptance, memory-leak measurement or RF stability during drawer activity.
  No panic, stream-ended or restart observed. Visual appearance, photographs,
  owner approval and known-station tuning response remain pending.
- Current recommendation: **Home not yet verified**. Source/host/build,
  approved flash, boot/PSRAM/touch-controller initialization, actual receiver
  identification and strict five-minute stream gates passed. Physical UI and
  touch/navigation acceptance gates have not passed.

### Owner visual review and badge defect, 2026-10-14

- Owner reports the requested drawer test "works correctly"; record this as
  owner-reported interaction success, not a captured count of 50 events or a
  measured memory-leak result. Strict RF capture preceded these touch tests.
- Owner photo `codex-clipboard-a87ed2bf-8e3a-4626-a98d-c9c65cdb9e1b.jpg`
  shows wrong pink/red Orc badge colors while receiving IQ. PLASMA is visibly
  selected; the pink waterfall is not evidence of an independent color bug.
- Asset forensic renders identify the cause: original RGB565 bytes are
  little-endian. Big-endian interpretation reproduces the wrong physical
  badge; little-endian restores the original green artwork. Earlier static
  review inferred the wrong storage order from Tab5's display byte swap.
- Corrected only badge byte assembly in `home_v3_ui.cpp`; no asset, palette,
  radio or display-wide byte-order change. New build/flash validation pending.
  Recommendation: **Home requires changes**; owner badge approval remains open.
- One incremental rebuild passed (exit 0), app unchanged at 857,376 bytes,
  18% partition free; image revision v1.0–v1.99 and valid image hash verified.
  Only badge decode and app build metadata differ; bootloader/partition
  hashes remain unchanged. Earlier flashed binaries/config/ELF preserved in
  app-directory `home-v3-before-badge-fix/`.
- New app SHA-256:
  `20a67189d426e9d4ac4d0ba45509d599c8801483cf48fef1e7530609b204f974`.
  ELF SHA-256 `20f219e9d97d3a74df95aff406e907d9f9830ff0570a1cbe215f2af4a1d79a84`;
  version `v0.4.0-beta.1-34-g9ad124cf-dirt`. Source input: `9ad124cf` plus
  the single badge-byte-order correction. App-directory logs:
  `idf-build-home-v3-badge-fix.log`, `home-v3-badge-fix-image-info.log`,
  `home-v3-badge-fix-sha256.txt`. Original asset unchanged. No broad/host suite
  repeated for this two-byte assembly correction; both endian interpretations
  were rendered from actual asset bytes and compared with the owner photo.
- Photo SHA-256:
  `2c7d8c6fd16fc6c928bef4bd11857c118c8b51ebb4b4728655fccdd9e3bc1f1c`.
  Flash approval and physical green-badge verification still pending. Earlier
  strict RF PASS applies to the pre-correction image; no new RF result claimed.

### Discoverable drawer control, 2026-10-14

- Owner requests a visible indication of where to open dashboard navigation.
  Added cyan `MENU >` beneath the OrcSDR/HOME text, changing to `MENU <` when
  open. The full adjacent 155x67 brand area now toggles the drawer, in addition
  to the existing badge. Hint is inside that target; no plot/header resizing.
- Added the brand target to geometry validation. Existing v3 drawer/layout
  host compile and test passed with the same strict g++ flags; root logs
  `menu-cue-host-build.log`, `menu-cue-host-test.log`. `git diff --check` passed.
- One incremental native build passed, app 857,552 bytes; valid image for
  P4 v1.0–1.99. App version `v0.4.0-beta.1-35-g7153ac0f-dirt`, source input
  `7153ac0f` plus menu-cue/hit-target changes. Includes little-endian badge fix.
  App SHA-256:
  `6df69ddf6f3ba517836ec4fe381121fe7fdb58a3a535dd4791e7ab745dde03be`.
  App-directory evidence: `idf-build-menu-cue.log`, `home-v3-menu-image-info.log`.
  Badge-only app preserved as `badge-fix-before-menu.bin`. No radio changes.
- Not flashed. Final approval, physical green-badge and MENU cue inspection,
  header-tap routing and drawer state feedback remain pending. Existing strict
  RF PASS remains evidence for the earlier flashed image, not this new image.

### Approved badge/menu flash and short check, 2026-10-14

- Owner approved combined correction/cue source `0e5e19f6837fdbf036c1c0bc1224d45a703e4463`. COM40 re-probe matched P4 v1.3, MAC `e8:f6:0a:e4:43:02`, 32 MB flash. All three image hashes matched the preceding build records. Generated P4 recipe flashed with all write hashes verified; no C6/NVS image, full erase or power-fixture change.
- Local script `flash_home_v3_menu_capture.py` checks image hashes and captures UART before controlled reset. Boot confirms 200 MHz PSRAM memory-test pass, GT911 at `0x5D`, Home v3 and boot complete. App version `v0.4.0-beta.1-35-g7153ac0f-dirt`, ELF prefix `ea941dddb`.
- Initial USB EP0 short-device-descriptor failure at 2075 ms recovered: raw/accepted counts both 1 at 13315 ms, profile `blog_v4l_r828s`, RF START at 14725 ms. Retain this transient error; it did not prevent receiver startup.
- Short check: 61 IQ rows over **45.06 seconds**, post-warmup **959,923-960,334 SPS**, zero drops, no NAV events. From app directory, `python ../../tools/hardware/check_4b_home_log.py --log home-v3-menu-com40.log --seconds 45` passed, exit 0. This is a smoke check, not a new five-minute qualification.
- App-directory evidence: `home-v3-menu-preflash-com40.log`, `home-v3-menu-flash-com40.log`, `home-v3-menu-com40.log`, `home-v3-menu-short-check.log`. Serial SHA-256: `4cf847356a575d3b38e5b6fd5ec6607790be96cb4098029a8da891b75471e6a6`.
- Owner green-badge/MENU photo confirmation, brand-area touch routing and cue state feedback remain pending. Earlier drawer success was owner-reported for the prior image. Recommendation: **Home not yet verified**, awaiting corrected-image visual acceptance.

### Owner confirmation of badge and menu, 2026-10-14

- On the flashed `0e5e19f` source image, owner answers "yes it does" to the combined check: green badge, visible MENU cue, open/close via badge and OrcSDR/HOME text, and state feedback `MENU >` / `MENU <`. These specific visual/touch corrections are **owner-confirmed**.
- Earlier owner report also says the requested drawer exercise works correctly. This remains physical owner testimony, not an automatically captured 50-event count or memory-leak measurement.
- Badge/menu correction is accepted by owner. Overall Home qualification still distinguishes the prior 310.11-second strict RF PASS from the corrected image's 45.06-second smoke PASS. Known-station peak/tuning check, complete enabled-button/palette coverage, corrected-image five-minute/nav-under-load measurement and measured memory stability remain open. No blanket Tab5 feature parity or other-dashboard acceptance.

### Starter visual layout integrated, 2026-10-14 (not flashed)

- Owner requested integration after review, retaining our hiding drawer. Source base `9bea602df6850f6723cd2af16e7e01d3bfdceb09`; archive SHA-256 `7313f1ee4d5ed1b5bd80019def16c89140638a6040c22accdca8f5051eac9aa9`. ZIP supplies visual/layout reference only; its standalone C component was not imported or executed.
- Added dedicated hamburger, icon header controls, larger frequency/brand fonts, pixel plot headings, 680px-wide live plots and grouped controls. Retained little-endian original badge, shared palettes, existing modal hiding drawer/dismissal shield and controller-owned frequency/STEP/rate/drop snapshots. Header HOME dismisses navigation; unported audio/VIS/settings/mode/filter/gain remain disabled. No placeholder badge, fake battery, POCSAG state, radio-local state or claimed calibrated dBFS axis copied from starter.
- No changes to `main.cpp`, driver manifest, IQ acquisition or Tab5. Geometry/state test updated and passed using `g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -Iapps/orcsdr-waveshare-4b/main apps/orcsdr-waveshare-4b/tests/home_v3_drawer_host.cpp -o home_v3_drawer_host`, then `./home_v3_drawer_host` through WSL. Logs `starter-layout-host-build.log`, `starter-layout-host-test.log` at checkout root. Font-advance checks pass for maximum frequency/title/STEP; log `starter-font-width-check.log`. These are not LVGL raster/touch acceptance.
- Root `starter-source.patch` SHA-256 `f13eba342dded6b4fc8fadfff6d4cf6dea6abca51ef6112879d38b9b9f15d4c8` identifies renderer, geometry, font-default and test inputs. One fresh ESP-IDF 5.5.4 build passed, exit 0, using the README's `build-4b-home-starter` command and rev1.x overlay. Existing image directories preserved. Same recorded local SDK modification/toolchain as prior builds.
- App **990,368 bytes**, 58,208 bytes free (6%) in unchanged 1 MiB factory slot; bootloader 23,952 bytes. Both images validate for P4 v1.0-v1.99. Generated config: PSRAM 200 MHz, flash 32 MB, external hubs and required fonts enabled. Original badge and palette symbols plus all four font symbols verified in ELF.
- App version `v0.4.0-beta.1-38-g9bea602d-dirt`, ELF SHA-256 `3fe54a286ebf4d06e38d00274e7a99c5f7ef0da7055cc7c648fb873836d3c1ab`. Build is base plus reviewed integration patch, not an unmodified historical binary.
- Image SHA-256: app `f4186fd79d12a4b15d34c6973580d525b37f7fb733ce0db89e472673fea12361`; bootloader `42251479678d88c73bcd05a8406ea1a14d31412ca756f02e904eaa5334b9826b`; partition `7f00b6c042a89b15b0cac534f82ed988caf29278ff5700b0c511eb1b5bb7c820` (unchanged). Generated recipe retains bootloader 0x2000, partition 0x8000, app 0x10000, DIO/80MHz/32MB. No C6 or NVS image.
- App-directory evidence: `idf-build-home-starter.log`, `home-starter-image-info.log`, `home-starter-bootloader-info.log`, `home-starter-sha256.txt`, generated config/lock/recipe. `git diff --check` passed. No broad suites or hardware actions performed for this integration.
- Recommendation: **Home not yet verified for the starter layout**. Final flash approval, actual screenshots/owner comparison, all new hit targets and hiding-drawer routing, and RF stability during the changed renderer remain pending. Prior layout's owner acceptance and earlier RF measurements remain historical evidence, not blanket acceptance of this image.

### Approved starter-layout flash, 2026-10-14

- Owner explicitly approved flashing source `dbd5e30c33a0ddc5cad049ca38eab2d4d86117e0`. COM40 re-probe matched P4 v1.3, MAC `e8:f6:0a:e4:43:02`, 32 MB flash; app/bootloader/partition hashes matched the starter build record. Generated `build-4b-home-starter/flasher_args.json` used through esptool 4.12.0 at 460800 baud; every write hash verified. No full erase, C6/NVS image or power-fixture change.
- Local `flash_home_starter_capture.py` checks hashes before writing and opens UART at 115200 before controlled RTS reset. Evidence beside command: `home-starter-preflash-com40.log`, `home-starter-flash-com40.log`, `home-starter-com40.log`, `home-starter-short-check.log`, `home-starter-measurement.json`.
- Boot confirms 200 MHz PSRAM memory-test pass, GT911 at 0x5D, starter-layout 680px renderer, ELF prefix `3fe54a286`, app version `v0.4.0-beta.1-38-g9bea602d-dirt`, and boot complete. Source-image metadata refers to the base plus integration patch recorded above.
- USB reaches raw_devices=1/accepted_sdr=1, identifies `blog_v4l_r828s`, completes initialization despite EP0 STALL messages, and logs RF START at 4834 ms: 96.1 MHz, 960 kS/s, 6x16KiB transfers.
- Short capture: **45.15 seconds**, 61 IQ rows, post-warmup **959,647-960,132 SPS**, zero added drops, consumed bytes monotonic. `python ../../tools/hardware/check_4b_home_log.py --log home-starter-com40.log --seconds 45` passed, exit 0. This is not a five-minute qualification. No NAV/TUNE events during capture.
- Serial-log SHA-256 `f1fb04ea78495d7365689c2a6aee86a71988345b56052f4ee23221970c0133b0`. Owner visual review/photo, new hit targets and drawer-under-load behavior remain pending. Earlier layout acceptance and five-minute evidence do not automatically qualify this layout. Recommendation: **Home not yet verified** for the starter layout.
