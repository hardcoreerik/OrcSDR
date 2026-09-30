# OrcSDR review context

OrcSDR is native ESP-IDF 5.5.4 firmware for the M5Stack Tab5 (ESP32-P4 host, ESP32-C6 Wi-Fi coprocessor over
ESP-Hosted SDIO) driving an RTL-SDR Blog V4 over USB. Review against these rules. Violations are findings.

## Hard rules

- Native ESP-IDF 5.5.4 only. PlatformIO is not a supported build or flash path for `apps/orcsdr-tab5`.
- `esp_rtl_sdr` is a pinned external dependency (`apps/orcsdr-tab5/main/idf_component.yml`). Never copy, fork
  or edit driver source into this repo. A driver bump is its own deliberate change, never bundled with a feature.
- No dynamic allocation, filesystem or SD I/O, or M5GFX drawing inside the RTL IQ callback or any decode hot path.
- Internal-DRAM budget: a large global or function-local `static` on the P4 must not sit in plain internal BSS;
  use PSRAM (`heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` or `EXT_RAM_BSS_ATTR`). A function-local `static Foo x{};`
  reserves its full size at link time even if never used, and only fails as a boot-time "Could not reserve
  internal/DMA pool" abort on a real device. Say whether a new static is plain, function-local, or PSRAM-backed.
- Host-tested core modules (anything with a runner in `tools/test-*.sh`, for example the scanner, catalog,
  keyboard and filter modules) must build with plain g++: no FreeRTOS, M5GFX or ESP-IDF headers unless guarded by
  `#if defined(ESP_PLATFORM)`.
- `ScreenController` (`apps/orcsdr-tab5/ui/screen_controller.*`) owns framebuffer and touch routing. A new
  dashboard goes through it; it must not draw directly or bypass it.
- `main.cpp` is integration glue only. Feature logic (scanning, parsing, state machines, rendering) belongs in
  the feature's own module.

## Hardware and runtime traps that have already bitten this project

- The SD card (SDMMC slot 0) and the C6 (SDMMC slot 1) share one controller. `esp_restart()` is a CPU-level reset
  on the P4 and left the SD/SDMMC path wedged about 1 restart in 5; restarts go through `orcsdr_restart_clean()`.
- SD reads must be chunked. `File::available()` seeks to the end and back, so calling it per line on a large file
  is quadratic and looks like a hang. Never scan a large file on the UI thread in a loop without bounding it.
- ESP-Hosted: the P4 host library is 3.0.6. A 3.0.6 host works with a C6 on 2.12.6 or 3.0.6. Do not reintroduce a
  check that requires an exact C6 version: it breaks M5 Launcher users (Launcher runs a 2.x host).
- Airband and other scanners: a squelch must use in-channel carrier versus a tracked noise floor, never wideband
  IQ power (it is AGC-regulated and cannot tell an idle channel from a busy one).
- Signal or settings state read from the UI task and written from another must be protected (spinlock or atomic).
- Every persistent setting needs a key that cannot collide with an existing meaning (old values must not be
  misread after a format change).

## Evidence discipline

Never claim or imply "hardware verified" without a named on-device run. "Builds", "host-tested" and "verified on
device" are three different claims. Flag any commit message, comment or doc that overclaims relative to what the
diff proves. Documentation must match behaviour (`tools/check_documentation_truth.py` runs in CI).

## Conventions

- Merge with merge commits; never squash or rebase shared branches. Release tags are annotated and never moved.
- Do not add dead code, TODO stubs, or unrelated cleanup to a focused change.
- Serial commands that change device state require authentication and must be documented in
  `docs/API_SERIAL_CLI.md`.
