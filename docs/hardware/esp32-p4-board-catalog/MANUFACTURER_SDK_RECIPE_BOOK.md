# Manufacturer SDK recipe book — no build claims

Snapshot 2026-10-28. Each recipe is a **proposed reproducible local run**, not work executed in this web session. Read RSA `AGENTS.md`; run `python rsa.py doctor` and `python rsa.py list` first. A project found on GitHub is not necessarily licensed for copying or analysis. For each import, pin commit, log `target.json`, dependencies and component license, source path, file hash, and source date.

## Common non-destructive build routine

Use the vendor sample inside RSA `targets/<slug>/source/`, not in OrcSDR. On NewcorePC, first activate the actual compatible IDF 5.5.4 environment and record `idf.py --version`, `git -C $env:IDF_PATH rev-parse HEAD`, and `riscv32-esp-elf-gcc --version`. Build each sample in its own clean working directory with source revision and configuration recorded.

```powershell
Set-Location F:\AI\RSA-Sandbox
python rsa.py doctor
python rsa.py list
# With IDF 5.5.4 environment activated, inside each copied vendor example:
idf.py --version
idf.py set-target esp32p4
idf.py reconfigure
idf.py build
if ($LASTEXITCODE -ne 0) { throw "BUILD FAILED: inspect and record toolchain + log" }
# Preserve stdout/stderr, sdkconfig, dependencies.lock, build/project_description.json,
# build/compile_commands.json, and SHA256 of the resulting ELF in the ignored evidence area.
# Do not flash; do not auto-label a successful build as a working board.
```

**Do not execute `idf.py set-target` across a dirty vendor project without reviewing the changes it makes**. Use an RSA-managed copied source working tree; preserve original source commit, sdkconfig and git status. `rsa.py` remains the entrypoint for all external imports/analyses, and any limitation must be recorded rather than bypassed.

## Espressif ESP-BSP

- Upstream: `https://github.com/espressif/esp-bsp`
- Pinned commit: `9112bd334726368d4aa4421dd01f2e4b1a739568`
- License gate: Apache-2.0; root BSP component LICENSE observed
- Example paths / candidates: `bsp/m5stack_tab5; bsp/esp32_p4_function_ev_board; examples/display; examples/display_usb_hid; examples/display_sdcard`
- Target evidence files: `bsp/m5stack_tab5/include/bsp/m5stack_tab5.h; bsp/m5stack_tab5/src/bsp_display.c; bsp/m5stack_tab5/src/bsp_usb.c; bsp/esp32_p4_function_ev_board/include/bsp/esp32_p4_function_ev_board.h`
- IDF/compatibility concern: ESP-IDF >=5.4 from both manifests; target test IDF 5.5.4; panel SDK could use managed dependencies
- Local `rsa.py` import/analyze: **NOT RUN in this session**.
- Local ESP-IDF 5.5.4 build: **NOT RUN; no build exit code available**.

```powershell
Set-Location F:\AI\RSA-Sandbox
python rsa.py import github:espressif/esp-bsp@9112bd334726368d4aa4421dd01f2e4b1a739568 --name esp-bsp-p4-catalog
python rsa.py analyze esp-bsp-p4-catalog
```

For reproducibility: save `rsa.py` run ID, full source SHA and top-level license receipt, source file paths + exact line numbers, sample `sdkconfig.defaults` and lockfile, IDF/toolchain versions, terminal output with exit code, ELF/image SHA-256 when actually built, then re-run `rsa.py analyze ... --force` if supported. Keep all build artifacts gitignored. Only mark a tracker field after the evidence receipt exists.

## Espressif ESP-Hosted-MCU

- Upstream: `https://github.com/espressif/esp-hosted-mcu`
- Pinned commit: `78b1c1c43c02b0c4e898cf87f94679278b1ddaff`
- License gate: Apache-2.0 confirmed
- Example paths / candidates: `examples/; host/; coprocessor/; port/`
- Target evidence files: `idf_component.yml; host/; coprocessor/; docs/`
- IDF/compatibility concern: Component version 3.0.10; IDF >=5.5. Match C6 CP firmware image manifest and host transport; never infer factory image compatibility
- Local `rsa.py` import/analyze: **NOT RUN in this session**.
- Local ESP-IDF 5.5.4 build: **NOT RUN; no build exit code available**.

```powershell
Set-Location F:\AI\RSA-Sandbox
python rsa.py import github:espressif/esp-hosted-mcu@78b1c1c43c02b0c4e898cf87f94679278b1ddaff --name esp-hosted-mcu-p4-catalog
python rsa.py analyze esp-hosted-mcu-p4-catalog
```

For reproducibility: save `rsa.py` run ID, full source SHA and top-level license receipt, source file paths + exact line numbers, sample `sdkconfig.defaults` and lockfile, IDF/toolchain versions, terminal output with exit code, ELF/image SHA-256 when actually built, then re-run `rsa.py analyze ... --force` if supported. Keep all build artifacts gitignored. Only mark a tracker field after the evidence receipt exists.

## M5Stack M5GFX

- Upstream: `https://github.com/m5stack/M5GFX`
- Pinned commit: `6e53f9bfcf7dae34e7c7c089cfd66db977793e17`
- License gate: License NOT CHECKED in this pass; check LICENSE before importing
- Example paths / candidates: `examples/; src/ (actual project paths to be confirmed)`
- Target evidence files: `M5GFX board selection code; esp32 P4/Tab5 drivers to locate`
- IDF/compatibility concern: Build path may be Arduino-centric; do not infer an ESP-IDF component exists
- Local `rsa.py` import/analyze: **NOT RUN in this session**.
- Local ESP-IDF 5.5.4 build: **NOT RUN; no build exit code available**.

**BLOCKED:** do not run an import of source until the license/approval gate is closed. Public documentation inspection only.

For reproducibility: save `rsa.py` run ID, full source SHA and top-level license receipt, source file paths + exact line numbers, sample `sdkconfig.defaults` and lockfile, IDF/toolchain versions, terminal output with exit code, ELF/image SHA-256 when actually built, then re-run `rsa.py analyze ... --force` if supported. Keep all build artifacts gitignored. Only mark a tracker field after the evidence receipt exists.

## M5Stack M5Unified

- Upstream: `https://github.com/m5stack/M5Unified`
- Pinned commit: `565a499675b4cbbf897451d7f9ef66890e2cbb90`
- License gate: License NOT CHECKED in this pass; check LICENSE before importing
- Example paths / candidates: `examples/; src/ (paths to verify)`
- Target evidence files: `board detection, power, touch and M5GFX glue`
- IDF/compatibility concern: Board revision detection and P4 support must be confirmed in source first
- Local `rsa.py` import/analyze: **NOT RUN in this session**.
- Local ESP-IDF 5.5.4 build: **NOT RUN; no build exit code available**.

**BLOCKED:** do not run an import of source until the license/approval gate is closed. Public documentation inspection only.

For reproducibility: save `rsa.py` run ID, full source SHA and top-level license receipt, source file paths + exact line numbers, sample `sdkconfig.defaults` and lockfile, IDF/toolchain versions, terminal output with exit code, ELF/image SHA-256 when actually built, then re-run `rsa.py analyze ... --force` if supported. Keep all build artifacts gitignored. Only mark a tracker field after the evidence receipt exists.

## LilyGO T-Display-P4

- Upstream: `https://github.com/Xinyuan-LilyGO/T-Display-P4`
- Pinned commit: `7b021f795f6842fe6d62d3b2f2931cd0f03d3f80`
- License gate: GPL-3.0 at root LICENSE
- Example paths / candidates: `main/examples/esp32c6_at_host_sdio_wifi; main/examples/esp32c6_at_host_sdio_uart; main/examples/es8311_sd_wav; main/examples/ethernet_iperf`
- Target evidence files: `main/idf_component.yml; main/Kconfig.projbuild; sdkconfig.defaults; project/T-Display-P4_*.pdf; libraries/lilygo_device_driver pinned submodule`
- IDF/compatibility concern: IDF >=5.5.4 in main manifest. AT-over-SDIO demo is not necessarily ESP-Hosted; pin submodule separately and verify licenses of each submodule
- Local `rsa.py` import/analyze: **NOT RUN in this session**.
- Local ESP-IDF 5.5.4 build: **NOT RUN; no build exit code available**.

```powershell
Set-Location F:\AI\RSA-Sandbox
python rsa.py import github:Xinyuan-LilyGO/T-Display-P4@7b021f795f6842fe6d62d3b2f2931cd0f03d3f80 --name lilygo-t-display-p4-catalog
python rsa.py analyze lilygo-t-display-p4-catalog
```

For reproducibility: save `rsa.py` run ID, full source SHA and top-level license receipt, source file paths + exact line numbers, sample `sdkconfig.defaults` and lockfile, IDF/toolchain versions, terminal output with exit code, ELF/image SHA-256 when actually built, then re-run `rsa.py analyze ... --force` if supported. Keep all build artifacts gitignored. Only mark a tracker field after the evidence receipt exists.

## LilyGO board-driver submodule

- Upstream: `https://github.com/Xinyuan-LilyGO/lilygo_device_driver`
- Pinned commit: `a47cf73e2c9d442c0f00b618749150f4482dcd0a`
- License gate: GPL-3.0 via LICENSE
- Example paths / candidates: `src/device/t_display_p4/`
- Target evidence files: `src/device/t_display_p4/t_display_p4_config.h; t_display_p4_driver.cpp`
- IDF/compatibility concern: Import as own RSA target pinned to parent gitlink SHA a47cf73e2c9d442c0f00b618749150f4482dcd0a
- Local `rsa.py` import/analyze: **NOT RUN in this session**.
- Local ESP-IDF 5.5.4 build: **NOT RUN; no build exit code available**.

```powershell
Set-Location F:\AI\RSA-Sandbox
python rsa.py import github:Xinyuan-LilyGO/lilygo_device_driver@a47cf73e2c9d442c0f00b618749150f4482dcd0a --name lilygo-device-driver-p4-catalog
python rsa.py analyze lilygo-device-driver-p4-catalog
```

For reproducibility: save `rsa.py` run ID, full source SHA and top-level license receipt, source file paths + exact line numbers, sample `sdkconfig.defaults` and lockfile, IDF/toolchain versions, terminal output with exit code, ELF/image SHA-256 when actually built, then re-run `rsa.py analyze ... --force` if supported. Keep all build artifacts gitignored. Only mark a tracker field after the evidence receipt exists.

## Olimex ESP32-P4-DevKit

- Upstream: `https://github.com/OLIMEX/ESP32-P4-DevKit`
- Pinned commit: `26705d36407a07324348927dfd30fbf4ffc1d94c`
- License gate: Hardware CERN-OHL-S-2.0; repo LICENSE GPL-3.0; per prior REA report
- Example paths / candidates: `SOFTWARE/Demo_Examples/mipi_dsi; SOFTWARE/Demo_Examples/display_lvgl_demos; SOFTWARE/Demo_Examples/sdmmc`
- Target evidence files: `HARDWARE/; SOFTWARE/; DOCUMENTS/`
- IDF/compatibility concern: An October 16 RSA/REA PCB analysis exists. Verify current target imported before creating duplicates; this is not a C6 console
- Local `rsa.py` import/analyze: **NOT RUN in this session**.
- Local ESP-IDF 5.5.4 build: **NOT RUN; no build exit code available**.

```powershell
Set-Location F:\AI\RSA-Sandbox
python rsa.py import github:OLIMEX/ESP32-P4-DevKit@26705d36407a07324348927dfd30fbf4ffc1d94c --name olimex-p4-devkit-catalog
python rsa.py analyze olimex-p4-devkit-catalog
```

For reproducibility: save `rsa.py` run ID, full source SHA and top-level license receipt, source file paths + exact line numbers, sample `sdkconfig.defaults` and lockfile, IDF/toolchain versions, terminal output with exit code, ELF/image SHA-256 when actually built, then re-run `rsa.py analyze ... --force` if supported. Keep all build artifacts gitignored. Only mark a tracker field after the evidence receipt exists.

## Elecrow Advanced 5-inch (documentation only)

- Upstream: `https://github.com/Elecrow-RD/-CrowPanel-Advanced-5inch-ESP32-P4-HMI-AI-Display-800x480-IPS-Touch-Screen`
- Pinned commit: `421a2d8c797f04151337ffd3ae03ad50bee0ac28`
- License gate: No top-level LICENSE observed; source permission unverified
- Example paths / candidates: `example/; factory_sourcecode/ (DO NOT IMPORT until license verified)`
- Target evidence files: `readme.md; Eagle_SCH&PCB/`
- IDF/compatibility concern: Inspect public readme/schematic documentation only; DO NOT import source, analyze vendor executables or build unless permissions established
- Local `rsa.py` import/analyze: **NOT RUN in this session**.
- Local ESP-IDF 5.5.4 build: **NOT RUN; no build exit code available**.

**BLOCKED:** do not run an import of source until the license/approval gate is closed. Public documentation inspection only.

For reproducibility: save `rsa.py` run ID, full source SHA and top-level license receipt, source file paths + exact line numbers, sample `sdkconfig.defaults` and lockfile, IDF/toolchain versions, terminal output with exit code, ELF/image SHA-256 when actually built, then re-run `rsa.py analyze ... --force` if supported. Keep all build artifacts gitignored. Only mark a tracker field after the evidence receipt exists.

## Elecrow Advanced 7-inch (documentation only)

- Upstream: `https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen`
- Pinned commit: `a25ecadb4b4d28e9e904e114a6a1b82ae6f84624`
- License gate: No top-level LICENSE observed; source permission unverified
- Example paths / candidates: `example/; factory_sourcecode/ (DO NOT IMPORT until license verified)`
- Target evidence files: `readme.md; Eagle_SCH&PCB/1.2/readme.md; .sch/.brd`
- IDF/compatibility concern: Resolve v1.0/v1.1 SDIO reorder, v1.2 SDMMC and RF conflicts; device HID demo ≠ host port
- Local `rsa.py` import/analyze: **NOT RUN in this session**.
- Local ESP-IDF 5.5.4 build: **NOT RUN; no build exit code available**.

**BLOCKED:** do not run an import of source until the license/approval gate is closed. Public documentation inspection only.

For reproducibility: save `rsa.py` run ID, full source SHA and top-level license receipt, source file paths + exact line numbers, sample `sdkconfig.defaults` and lockfile, IDF/toolchain versions, terminal output with exit code, ELF/image SHA-256 when actually built, then re-run `rsa.py analyze ... --force` if supported. Keep all build artifacts gitignored. Only mark a tracker field after the evidence receipt exists.

## Other manufacturers

- Waveshare: Start with [WIFI6 DEV KIT](https://docs.waveshare.com/ESP32-P4-WIFI6-DEV-KIT), [Module DEV KIT](https://docs.waveshare.com/ESP32-P4-Module-DEV-KIT), and exact Rev1.1/Rev1.2 [jumper FAQ](https://docs.waveshare.com/ESP32-P4-WIFI6-DEV-KIT/FAQ); locate separately licensed GitHub BSP/example source **before import**. Record USB Host switch and VBUS rail. License/repo not pinned: BLOCKED for import.
- DFRobot: [DFR1172 official wiki](https://wiki.dfrobot.com/dfr1172/). Locate open example repository, license and PCB revision first; check early C6 firmware lot. No source import in this session.
- Seeed: [D1001 official wiki](https://wiki.seeedstudio.com/getting_started_with_reterminal_d1001/). Locate independent SDK and matching screen/controller revision, license, sample build path. No source import.
- Adafruit, Makerfabs, Guition/JC and smaller vendors: primary source and full product identity still required before any code import. Do not invent GitHub repositories.

## Version-upgrade procedure

1. Compare upstream new commit and pinned commit via GitHub compare, and review license change.
2. Diff relevant board definitions, DSI init arrays, touch reset, power switches and SDIO labels; capture both old/new line URLs.
3. Add a **new evidence observation**, not an overwrite that hides the old board revision.
4. Run `python rsa.py import ...@NEW_SHA` (respecting its caching semantics), `rsa.py analyze`, plus clean IDF build if supported, and update exact logs.
5. Reconcile conflicts with schematic and real board marking; require physical log before `VERIFIED_RUNTIME`.
6. Commit inside RSA only, review `python rsa.py publish --dry-run` and use approved publish path; no merges without consent.