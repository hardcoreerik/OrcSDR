# ESP32-P4 board compatibility research for OrcSDR

**Status:** research / verification in progress. **Snapshot started:** 2026-10-28. **Branch:** `research/esp32-p4-board-catalog-20261028`. **Scope:** documentation and evidence only; no firmware or SDK integration here.

## Goal

Establish one **revision-correct, manufacturer-evidenced hardware profile** per commercially obtainable ESP32-P4 PCB or electrically distinct product revision. Sales bundles/screens must be tracked separately from PCB revisions. Support full OrcSDR consoles (MIPI-DSI touch display + powered USB host for RTL-SDR + ESP32-C6 ESP-Hosted SDIO Wi-Fi) and independent headless OrcSDR/OrcNode use cases.

All pin numbers, I2C device addresses, panel controllers, bootstrapping, voltage rails, ESP-Hosted versions, LoRa/GNSS pin maps, silicon revisions, prices and stock claims require individually traceable primary evidence. **An example configuration is not an electrical measurement.**

## Deliverables / filenames

1. [LOCAL_RSA_REA_SESSION_PROMPT.md](LOCAL_RSA_REA_SESSION_PROMPT.md) — paste into the separate local RSA session; obeys that repository's AGENTS.md.
2. [MANUFACTURER_SDK_QUEUE.md](MANUFACTURER_SDK_QUEUE.md) — vetted manufacturer/BSP entry points, expected evidence and build probes.
3. [BOARD_VERIFICATION_TRACKER.md](BOARD_VERIFICATION_TRACKER.md) — board inventory, revision gates, and explicit signoff criteria.
4. [EVIDENCE_PROTOCOL.md](EVIDENCE_PROTOCOL.md) — claim schema, authoritative source precedence, conflict and reproducibility rules.

Proposed machine-readable outputs for the subsequent evidence integration: `board_catalog.json`, `BOARD_MATRIX.md`, `UNVERIFIED_REGISTER.md`, `SOURCE_CONFLICTS.md`, `SDK_RUN_LOG.md`, plus optional `profiles/<board-id>_<revision>.md` when enough facts exist. Do not silently claim these proposed filenames were committed or validated yet.

## Risk model

- **P0 electrically dangerous:** USB VBUS power sourcing/reverse-power, battery charge/ship mode, 3.3-V GPIO, power enables.
- **P0 false compatibility:** USB-C flashing device misnamed as USB host; DSI connector mistaken for installed panel; RGB/SPI panel mistaken for DSI; ESP32-C6 chip mistaken for working ESP-Hosted firmware; 4-wire SDIO pin swaps across revisions.
- **P1 boot-critical:** panel reset, DSI clock/lane count/rate, touch address/INT sequence, I2C expander bits, PMIC supplies, NVS partition constraints.
- **P1 product quality:** SD-card conflicts with C6 SDIO, USB host power budget during RTL-SDR peak current, panel memory/PSRAM bandwidth and thermals.
- **P2 enhancements:** audio, IMU, RTC, battery gauge, LoRa/GNSS, Ethernet/PoE, camera.

## Compatibility gate

`FULL_CONSOLE_VERIFIED` requires (1) exact PCB+panel revision identified, (2) display controller/timing/power proven, (3) known working **powered** USB host port with RTL-SDR enumeration+continuous I/Q, (4) Wi-Fi proven over the board's actual C6/C5 SDIO wiring and recorded compatible ESP-Hosted host/CP versions, (5) no boot/resource collision under ESP-IDF 5.5, and (6) reproducible logs. Boards satisfying hardware descriptions only are `FULL_CONSOLE_CANDIDATE` / `CONDITIONAL`; headless devices are useful independently and should not be described as failures.

## Source discipline

1. Manufacturer schematic/PCB/netlist for electrical wiring (match exact revision).
2. Vendor pin header/BSP actually compiled for that revision; report discrepancies with schematic rather than overriding schematic silently.
3. Vendor official documentation and dated errata.
4. Real hardware logs on a physically identified board, including ESP-IDF build and boot logs.
5. General processor reference manuals for capabilities only, never for board-level connection proof.

`VERIFIED_DOCUMENTARY`, `VERIFIED_RUNTIME`, `INFERRED`, `UNVERIFIED`, `CONFLICT`, `NOT_APPLICABLE` are distinct statuses. Include dated URLs or pinned commit+file+line and hardware revision with every positive claim.

## Example high-risk revision exceptions

- Espressif Function-EV-Board **v1.4 versus v1.5.2**: port and GPIO exposure changed; board version **is not** silicon stepping. [Espressif guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/user_guide.html).
- LilyGO **T-Display-P4 v1.0 TFT versus AMOLED and v2.0 evaluation preview**: distinct panel ICs, touch, and not-yet-established production BOM. [Vendor repository](https://github.com/Xinyuan-LilyGO/T-Display-P4).
- Elecrow CrowPanel Advanced 7-inch **v1.0/v1.1 SDIO** and **v1.2 radio GPIO**: version-specific pin changes; stock status per revision not assumed. [Vendor repository](https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen).
- Elecrow **5-inch is RGB, not DSI** per the vendor's explicit RGB pin definitions; do not carry over 7-inch DSI setup. [Vendor repository](https://github.com/Elecrow-RD/-CrowPanel-Advanced-5inch-ESP32-P4-HMI-AI-Display-800x480-IPS-Touch-Screen).
- M5Stack Tab5 panel variants are identified through board labeling and vendor BSP; do not assume one panel init table globally. [Vendor product docs](https://docs.m5stack.com/en/core/Tab5).

## Rules for parallel sessions

- This branch is a research worktree, not a replacement for any active OrcSDR firmware branch. Do not merge without explicit user approval.
- In local `F:\AI\RSA-Sandbox`, **RSA-Sandbox AGENTS.md forbids modifying OrcSDR or other outside directories**. The local RSA agent must only read/import OrcSDR and produce evidence/handoff inside its approved sandbox, then publish reports using the approved REA pipeline. A separate explicitly authorized OrcSDR integration actor can apply the handoff into this branch.
- Closed/commercial **firmware binaries or SDKs** require explicit prior approval per RSA/REA policies. Publicly documented pinout, open-source vendor examples and build configurations can be researched within their licenses. Never flash or execute downloaded vendor firmware as a research shortcut.
