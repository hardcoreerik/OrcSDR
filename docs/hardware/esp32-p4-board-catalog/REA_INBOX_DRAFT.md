---
title: "ESP32-P4 manufacturer BSP, pinmap and revision documentary audit — preliminary handoff"
date: "2026-10-28"
targets: ["esp32-p4-devkit", "orcsdr-hardcoreerik"]
tags: ["esp32-p4", "orcsdr", "bsp", "schematics", "usb-host", "sdio", "hardware-audit"]
sources:
  - "https://github.com/espressif/esp-bsp/blob/9112bd334726368d4aa4421dd01f2e4b1a739568/bsp/m5stack_tab5/src/bsp_display.c"
  - "https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/a47cf73e2c9d442c0f00b618749150f4482dcd0a/src/device/t_display_p4/t_display_p4_config.h"
  - "https://github.com/Elecrow-RD/-CrowPanel-Advanced-5inch-ESP32-P4-HMI-AI-Display-800x480-IPS-Touch-Screen/blob/421a2d8c797f04151337ffd3ae03ad50bee0ac28/readme.md"
  - "https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen/blob/a25ecadb4b4d28e9e904e114a6a1b82ae6f84624/Eagle_SCH&PCB/1.2/readme.md"
  - "https://github.com/hardcoreerik/REA-Results/blob/main/targets/esp32-p4-devkit/notes/pcb-lessons-2026-10-16.md"
key_findings:
  - "Tab5 BSP source detects ILI9881C/GT911, ST7123 or ST7121 (bsp_display.c, pinned esp-bsp commit)."
  - "LilyGO source declares XL9535 USB PHY/SD/C6 enable lines and separate SD/MMC vs C6 SDIO GPIO sets (t_display_p4_config.h, pinned driver submodule)."
  - "Elecrow 5-inch manufacturer README explicitly defines an RGB interface, not MIPI DSI."
  - "Elecrow 7-inch V1.2 in-repository maintenance guide reports SD card schematic-versus-example GPIO divergence; physical verification not done."
caveats:
  - "This draft is NOT an RSA-generated target or compiled-binary REA result; no new rsa.py imports or analyses were run."
  - "No IDF 5.5.4 firmware was built; no boards, C6 firmware, or powered USB host were tested."
  - "For source/schematic conflicts, original raw Eagle files still need independent verification."
  - "Do not publish as generated REA-Results findings or claim the source code has been analyzed with rsa.py."
---

# Preliminary ESP32-P4 SDK research handoff (unpublished draft)

This file is prepared as *review material only* for a local RSA/REA session. It must not be used to hand-edit `REA-Results/targets`, `comparisons` or generated indexes. The existing Oct 16 Olimex result remains the only referenced *previous* RSA/REA quantitative analysis in this topic. When the local `rsa.py` scans have actually produced verified metrics, the local operator should capture `target.json`, canonical `metrics/summary.json`, `findings.json`, toolchain and pinned SHA, commit them within RSA, and run `python rsa.py publish --dry-run`. Only the approved `rsa.py publish` path may publish generated REA reports. An inbox branch/merge is separately approval-gated.

Reference the attached `HANDOFF.md`, `SOURCE_CONFLICTS.md`, and `ESP32P4_CATALOG_HANDOFF.json` for field-level evidence and explicitly unverified acceptance gates.