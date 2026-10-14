# OrcSDR hardware selection

Decision: ONE repo, ONE OrcSDR product, shared DSP/decoder and radio engine.
Maintain multiple board-specific ESP-IDF builds and renderers. The standalone
esp-rtl-sdr driver remains a separately versioned reusable component.

- Existing apps/orcsdr-tab5 remains the released target.
- apps/orcsdr-waveshare-4b is a developer display/touch/driver-link probe,
  NOT full OrcSDR firmware or an installable board release.
- components/orcsdr_ui_layout is independent of M5GFX, LVGL or ESP-IDF.
- hardware/boards.json lists future board IDs and their readiness.
- Future ESP32-P4 headless builds use the shared radio/services without LCD.

Webflasher: select physical hardware first, then release channel, then connect.
ESP Web Tools can identify ESP32-P4 chipFamily, but NOT distinguish Tab5 from
Waveshare; two same-family firmware variants must NEVER share a manifest build
list. Only physical-device-verified boards get board-specific install manifests.
Do not silently write the C6 firmware and do not use merged images that
overwrite settings. Existing Tab5 browser flasher preserves NVS by flashing
separate bootloader, partition table and factory application sections.

Release test gates: native build; physical silicon and board revision; USB host
VBUS and dongle enumeration; five minutes >=95% effective IQ rate and bounded
drops; touch, display, audio, SD, C6/network coexistence; UI parity; settings
preservation; release manifest and image SHA-256. Distinguish source implemented,
host-compiled, P4-built, flashed and physically validated.

See REA_GRAPHICS_PORT.md for vendor source audit and IMU/portrait experiment.
