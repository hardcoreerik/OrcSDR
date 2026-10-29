# Local RSA-Sandbox / REA-Results session prompt — ESP32-P4 board evidence

> **Paste everything below into the separate local AI session.** This is an instruction for the local RSA lab, not authorization to alter OrcSDR firmware. Snapshot: 2026-10-28.

---

You are conducting an evidence-driven, **repeatable manufacturer SDK and BSP audit** for Oregon Radio Company's OrcSDR ESP32-P4 firmware hardware portability. Existing web research is on the `hardcoreerik/OrcSDR` documentation branch `research/esp32-p4-board-catalog-20261028`, under `docs/hardware/esp32-p4-board-catalog/`. The user wants to replace hardware speculation with source-verified, revision-specific board profiles and cross boards off a tracked completion list.

## Authorized scope and repository rules

1. First read **`F:\AI\RSA-Sandbox\AGENTS.md`**, `README.md`, relevant `rsa.py` help and tests; read `AGENTS.md` in private `hardcoreerik/REA-Results`; read OrcSDR documentation branch `research/esp32-p4-board-catalog-20261028` **read-only** and any relevant OrcSDR AGENTS.md. If the local checkouts differ, inspect and report, never overwrite a working tree.
2. `RSA-Sandbox/AGENTS.md` takes precedence for local operation. **Do not write outside `F:\AI\RSA-Sandbox`**. In particular, treat OrcSDR, esp-rtl-sdr and REA-Results checkouts as read-only unless `rsa.py publish` is explicitly authorized by those rules. Prepare clean Markdown/JSON handoff files under the sandbox so a separate OrcSDR integration actor can commit them to `research/esp32-p4-board-catalog-20261028`.
3. Do not merge any branches, push to OrcSDR main, change active firmware, flash boards, change tool configs, execute downloaded vendor programs, or analyze closed/commercial vendor SDKs or firmware without explicit prior approval. The request does authorize researching openly licensed manufacturer code and documentation; **license-check each imported project** before analyzing.
4. Run `python rsa.py doctor` first. Record the exact tool versions and failures. Use `rsa.py` for **every project/source/binary import, analysis and comparison**; do not hand-populate `targets/` or generated RSA metrics. Preserve original git remote, pinned commit SHA, release/tag, license, URL, retrieval date, and SHA-256 for every non-git source. Do not confuse a partial or failed scan with a successful scan.
5. REA is for **compiled binary / firmware architecture and software evidence**, not proof of wiring. Lack of ELF/firmware or an unsupported RISC-V provider must be recorded as "REA not run," never as "no findings." Manufacturer source code / schematics and device logs are separate evidence layers.
6. If there is a genuine need to inspect proprietary compiled factory firmware, first write the proposed scope, source and purpose in `notes/approval_requests/` and obtain Erik's explicit approval. No bypassing access controls or publishing third-party binaries. Leave everything downloaded from manufacturers local and gitignored, with receipts.

## Hardware goal and acceptance criteria

OrcSDR targets ESP-IDF 5.5.x (validate installed version and compatibility with ESP-IDF 5.5.4 if available), ESP32-P4, a **MIPI-DSI capacitive touchscreen**, a real **USB host with correctly supplied VBUS** for RTL-SDR V4, and `ESP-Hosted-MCU` with ESP32-C6/C5 over real SDIO wiring. Not all ESP32-P4 products have DSI displays: maintain a separate classification for RGB/SPI displays and headless modules. Do not count a USB device/flash/debug port or exposed D+/D- as a proven host connector.

For each electrically distinct PCB revision, record board/vendor/product URL/stock/price, P4 silicon stepping, flash/PSRAM, panel IC+resolution+orientation+MIPI lane count/rate+reset/backlight/power, touch I2C address+INT/RST, every I2C bus/device/address, C6/C5 exact part and SDIO CLK/CMD/D0-D3/reset+ESP-Hosted host/CP versions, USB host/device/OTG port roles+VBUS enable+PHY collisions, audio I2S+amplifier/mic, SDMMC/eMMC, RMII PHY/reset, LoRa/SX/LR variants+RF switch, GNSS UART, RTC/IMU/charger/gauge/camera/expander bits, power/errata, BSP/SDK license, and an OrcSDR capability grade.

**Never invent GPIOs.** Every nonempty factual field needs a manufacturer schematic/netlist, vendor driver header/code, official wiki, or Espressif BSP citation **next to the value**, specifying matching revision. Unknown is explicitly `UNVERIFIED`; source-code-proposed values may be `DOCUMENTED_IN_SDK` but are **not measured**. Conflicting evidence becomes `CONFLICT` with both citations and a bench test, not a guessed compromise.

## First phase — baseline and vendor SDK imports

- Read existing `docs/hardware/esp32-p4-board-catalog/README.md`, `MANUFACTURER_SDK_QUEUE.md`, `BOARD_VERIFICATION_TRACKER.md`, and `EVIDENCE_PROTOCOL.md` from `research/esp32-p4-board-catalog-20261028`. Extract the inventory and unresolved high-risk questions.
- Read REA-Results `INDEX.md`, `index.json` and `graph.json` to reuse existing evidence; note the source snapshot and any stale results.
- Run from PowerShell:
  ```powershell
  Set-Location F:\AI\RSA-Sandbox
  python rsa.py doctor
  python rsa.py list
  python rsa.py import github:espressif/esp-bsp@<PINNED_FULL_COMMIT> --name esp-bsp-p4-catalog
  python rsa.py analyze esp-bsp-p4-catalog
  ```
  Substitute verified commit IDs; placeholders must NEVER be used literally. Continue with `github:m5stack/M5Unified`, `github:m5stack/M5GFX`, `github:Xinyuan-LilyGO/T-Display-P4`, `github:OLIMEX/ESP32-P4-DevKit`, and both Elecrow 5/7-inch repositories, plus sources discovered by the SDK queue. Respect tool constraints and licenses. Do not import the same repository under multiple slugs unless its revisions materially differ; pin branches/releases.
- Inspect `idf_component.yml`, `Kconfig*`, `sdkconfig.defaults*`, `components/*`, `main/*`, board-specific `*_config.h`, `bsp_board.h`, peripheral init routines, panel reset tables, I2C expander writes, USB power switch control, and ESP-Hosted `cp/` images/manifests. Use static analysis/source search, keep a vendor path + line/commit + exact code constant in the claim's evidence. Do not treat default Kconfig as proof of the actual shipped revision.
- Import `espressif/esp-hosted-mcu` at a pinned commit. Map ESP-IDF constraints, SDIO bus width/clock, handshake, host-versus-coprocessor image compatibility, flash/update procedure. Record exact ESP-Hosted version from manifests/firmware metadata, or `UNVERIFIED`. Do not assert that "supports SDIO" means that shipped coprocessor firmware matches.
- Preserve source licenses and any redistributability restrictions. Third-party source code and binaries stay in the sandbox, not REA-Results or OrcSDR.

## Second phase — executable, **non-destructive** SDK validation

For each imported open-source SDK, identify available samples for display+touch, expander/power, SDMMC, ESP-Hosted, USB Host, Ethernet, audio, LoRa and GNSS. Keep a board/revision-specific `SDK_RUN_LOG.md`.

Run static build checks, *if the installed compatible toolchain and dependencies permit*, **without flashing a board**:

- Pin IDF version (prefer separate build environments for vendor's supported IDF and OrcSDR's 5.5.x). Document exact IDF commit, toolchain version, sdkconfig and dependency lock.
- `idf.py set-target esp32p4`, `idf.py reconfigure`, `idf.py build` for minimal panel/touch and (where available) USB Host, SDIO and ESP-Hosted demos. Keep stdout/stderr and `build/project_description.json`, `build/compile_commands.json`, `sdkconfig`, `dependencies.lock` (if generated), compiler version and actual build exit code in local evidence outputs.
- For failure, log failure stage, missing component, version mismatch, unsupported silicon revision, missing prebuilt library, license, and exact reproducer. Do not claim a board is incompatible merely because an example cannot build in one environment.
- Static source analysis can verify a pin constant in vendor source; it does not verify electronics. For silicon stepping, use Espressif errata and **read actual chip revision only during a separately authorized hardware observation**. Do not derive P4 chip revision from PCB version.
- If a binary is built or legitimately provided as open-source evidence, put it in the sandbox's supported `build/` area for `rsa.py` and REA, with checksums; run `python rsa.py analyze <slug> --force`. If REA provider limitations block analysis, attach the exact diagnostic.
- Propose, do not perform, USB VBUS / dongle power or battery tests. A powered USB dongle test needs approved hardware, known current limits, and a logged procedure.

## Third phase — close the board inventory systematically

Use the board tracker as the **single checklist**, with each record separate for PCB revision and panel configuration. At minimum verify: M5Stack Tab5, Stamp-P4, PoE-P4; Espressif Function-EV v1.4/v1.5.2 plus P4X/C5 variants, Core/EYE; Waveshare Module-DEV-KIT, WIFI6-ETH/POE, WIFI6-DEV-KIT, 3.5 SPI and 4.3/4B/5/7/8/10.1 DSI screen families and bundles; LilyGO TFT/AMOLED v1/v2 evaluation; Olimex DevKit and P4-PC; Seeed reTerminal D1001; DFRobot FireBeetle 2; Adafruit Metro Hydra; Makerfabs MaTouch; Elecrow 5/7/9/10.1 and variants; Guition/JC panels; additional real product pages from Hosyond, Sunton, Heltec, SparkFun, Banana Pi, Orange Pi or mark "no primary-source board confirmed" and keep them outside verified inventory.

Special conflict gates:
- **Elecrow 5-inch: RGB, not DSI**, unlike 7-inch; verify vendor source and resolve any earlier draft misclassification.
- **Elecrow 7-inch v1.0/v1.1:** C6 SDIO data-line reordering. **v1.2:** radio expansion pin changes and reported SDMMC schematic-versus-code discrepancy. Only a matching revision pin-map can be accepted.
- **LilyGO 4.05 TFT versus 4.1 AMOLED:** different panel/touch controllers. V2.0 evaluation preview is not automatically commercially released.
- **M5 Tab5:** at least two panel controller assemblies; select by actual device identification. Also verify C6 firmware and USB dongle power under load.
- **Espressif Function-EV:** v1.4 versus v1.5.2 USB routing and header GPIO changes; P4 chip stepping is a separate field.
- **DFRobot FireBeetle 2:** official warning about a C6 factory flash issue on an early production lot.
- **Waveshare USB OTG:** board jumpers and VBUS switches, without assuming a Type-C debug port provides host power.

Create one verification result for **every** board: `IDENTIFIED → SOURCE_CAPTURED → PINMAP_EXTRACTED → SDK_BUILD_CHECKED → HARDWARE_TESTED → PROFILE_READY`. Do not skip gates; use `BLOCKED` if hardware unavailable or a closed binary needs approval. A board may be marked `N/A` for a feature only when an authoritative schematic shows it is absent. Each row has owner/session, exact revision, timestamp, evidence links, outstanding blockers, next test and explicit signoff field. Checking a box requires a cited record, not just reading vendor marketing.

## Markdown and JSON outputs (required)

All local analysis writings must stay inside RSA-Sandbox's permitted folders; do not edit RSA-generated reports and metrics. Prepare these **handoff artifacts** (names can be nested under `notes/esp32-p4-catalog-handoff/` if that location is accepted by AGENTS.md):

1. `BOARD_CATALOG_HANDOFF.md` — verbose narrative and manufacturer comparison, ranking: console-ready / conditional / headless. The ranking must distinguish `documentary candidate` from physical testing.
2. `MANUFACTURER_SDK_RECIPE_BOOK.md` — one reproducible subsection per manufacturer with exact repo/ref/license, prerequisites, commands, sample path, pin-file location, build exit/logs, and how to update evidence after source revisions.
3. `BOARD_VERIFICATION_TRACKER.md` — all boards with progress gates, per-board checkboxes, next actions, evidence links and conditional blockers. Cross off only proven tasks, not an entire board when a single peripheral works.
4. `PINMAP_AND_PERIPHERAL_EVIDENCE.md` — field-by-field facts with primary URL and revision, including expander bits/I2C addresses. Source code values versus schematic nets must stay distinct.
5. `SOURCE_CONFLICTS.md` — every discrepancy, both citations, affected revisions, potential boot/voltage impact and required bench discriminator.
6. `UNVERIFIED_REGISTER.md` — exhaustive missing field list by board, plus explicit `INFERRED` facts and confirmation procedure.
7. `SDK_RUN_LOG.md` — exact `rsa.py` and `idf.py` commands, environment/build hashes, successes/failures, output artifacts.
8. `ESP32P4_CATALOG_HANDOFF.json` — portable machine-readable per-board data with `field.value`, `field.status`, `field.source_url`, `field.source_file`, `field.source_ref`, `field.board_revision`, `field.evidence_type`, `field.observed_on_hardware` where applicable; do not fabricate unavailable keys.
9. `HANDOFF.md` — files to import to OrcSDR research branch, SDK import/analysis runs completed, SHA256/provenance, P0 gaps and exact next local-session action.
10. `inbox/<YYYY-MM-DD>_esp32p4_board_sdk_audit.md` in **REA-Results**, only through an approved process: include YAML frontmatter per REA-Results AGENTS and links to metrics/findings; never paste manufacturer binaries/source or hand-edit generated reports/index.

All reports must distinguish **verified schematic**, **vendor SDK declaration**, **build passed**, **physical runtime tested**, **inference**, **unknown**, **conflict**. Keep an append-only evidence audit trail.

## Publishing and final handoff

- Commit relevant analysis notes locally, **inside RSA-Sandbox only**. Review diffs and secret scan. Do not push this local repository, per AGENTS.md.
- Run `python rsa.py publish --dry-run`; report exactly what it would publish. The only standing exception to the lab no-push rule is the approved `rsa publish --push` generated-results path, which must leave REA-Results private. For optional new `REA-Results/inbox/` findings, use an isolated branch or handoff under that repo's rules, no merge without Erik's explicit approval. Do not modify generated indexes/targets by hand.
- Preserve an import plan for the OrcSDR documentation branch, but **do not write to OrcSDR from the RSA lab**. The OrcSDR integration actor will apply the Markdown/JSON handoff on `research/esp32-p4-board-catalog-20261028` after reviewing conflicts. Never force push or merge to main.
- Finish with a quantitative scoreboard: boards inventoried, PCB revisions identified, pinmaps document-verified, SDKs pinned/imported, SDKs built, actual physical hardware validations, profiles ready, source conflicts open, missing fields, and exact blockers. State which steps were not run and why.

**Priority:** traceable evidence and reproducibility, not speed. If a manufacturer source is inaccessible or contradictory, leave it **UNVERIFIED/CONFLICT** with a concrete follow-up. Don't call the hardware profile complete before the P0 gates are passed.

---

### Key documentation URLs to start with

- [RSA-Sandbox](https://github.com/hardcoreerik/RSA-Sandbox), [REA-Results](https://github.com/hardcoreerik/REA-Results) (private).
- [Espressif ESP-BSP](https://github.com/espressif/esp-bsp/tree/master/bsp), [M5Stack Tab5 BSP](https://github.com/espressif/esp-bsp/tree/master/bsp/m5stack_tab5), [Function-EV BSP](https://github.com/espressif/esp-bsp/tree/master/bsp/esp32_p4_function_ev_board).
- [ESP-Hosted MCU](https://github.com/espressif/esp-hosted-mcu), [P4↔C6 emulator example](https://github.com/espressif/esp-emulator/blob/main/docs/guides/HOSTED.md).
- [M5Unified](https://github.com/m5stack/M5Unified), [M5GFX](https://github.com/m5stack/M5GFX), [M5 Tab5 docs](https://docs.m5stack.com/en/core/Tab5).
- [LilyGO T-Display-P4](https://github.com/Xinyuan-LilyGO/T-Display-P4), [Olimex P4-DevKit](https://github.com/OLIMEX/ESP32-P4-DevKit).
- [Waveshare Module-DEV-KIT](https://docs.waveshare.com/ESP32-P4-Module-DEV-KIT).
- [Elecrow 7-inch](https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen), [Elecrow 5-inch RGB](https://github.com/Elecrow-RD/-CrowPanel-Advanced-5inch-ESP32-P4-HMI-AI-Display-800x480-IPS-Touch-Screen), [Elecrow 9-inch](https://github.com/Elecrow-RD/CrowPanel-Advanced-9inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen).
- [DFRobot FireBeetle 2](https://wiki.dfrobot.com/dfr1172/), [Adafruit Metro Hydra](https://www.adafruit.com/product/6536), [Seeed reTerminal D1001](https://wiki.seeedstudio.com/getting_started_with_reterminal_d1001/).
