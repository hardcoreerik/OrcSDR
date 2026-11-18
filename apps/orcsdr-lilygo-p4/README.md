# OrcSDR — LilyGO T-Display P4 (experimental application)

**Hardware target:** LilyGO T-Display-P4 V1.0, **AMOLED RM69A10 + GT9895 + SX1262**.
The 4.05-inch TFT / HI8561 and LR2021 variants are **not** this target.

**Current state: safe headless boot only.** The build is an independent ESP-IDF application, but the LilyGO board profile is documentary rather than bench-verified. The native AMOLED backend and XL9535 power/reset sequencing are incomplete. For safety the LilyGO firmware does **not** start SDR USB host, SD, C6 Wi-Fi, audio or SX1262; it also does not render on the AMOLED. Do not call it a working SDR or a finished installer. Flashing replaces existing firmware and may require recovery.

## Prerequisites

- ESP-IDF **5.5.4**, installed and activated for the current shell (idf.py on PATH and IDF_PATH exported). PlatformIO is not supported.
- Git, Python from the active ESP-IDF environment (includes esptool and pyserial).
- The **exact board SKU**, its recovery instructions and the ESP32-P4 silicon revision. Secure boot/flash encryption could make ordinary backups unsuitable.
- A data-capable USB connection, a known ESP32-P4 serial port or a single identifiable USB serial device.

## Get source

~~~sh
git clone -b feature/lilygo-dedicated-app-20261117 https://github.com/hardcoreerik/OrcSDR.git
cd OrcSDR
~~~

Run the helper from the repository root **or any other working directory** by giving Python the path to the helper. It locates all project files from its own location; it does not hard-code Windows paths, COM ports, Linux tty names, or the caller's current directory.

## Detect the connected P4

~~~sh
python apps/orcsdr-lilygo-p4/tools/lilygo.py probe
~~~

The helper selects a serial port **only if exactly one supported USB-serial candidate exists**, and then verifies it is an ESP32-P4 and reads its revision using esptool. If ambiguous, use an actual port:

~~~sh
python apps/orcsdr-lilygo-p4/tools/lilygo.py probe --port <PORT>
~~~

A chip ID is NOT board-SKU identification. Check the physical LilyGO silkscreen and display/radio assembly before proceeding.

## Build without a board

~~~sh
python apps/orcsdr-lilygo-p4/tools/lilygo.py build --revision 1
~~~

Use --revision 3 for v3.x silicon. Separate build directories avoid mixing incompatible bootloaders. The helper automatically composes these default files in order:

1. Existing shared console sdkconfig.defaults (temporary legacy configuration source).
2. This app's sdkconfig.defaults (LilyGO profile).
3. This app's sdkconfig.silicon-v3 (only for v3.x P4).

The helper verifies LilyGO board selection and silicon settings after the IDF build. Builds will use the immutable USB-driver dependency pinned by the existing P4 console's component manifest. **A successful compile is not a hardware validation.**

## Backup and experimental flash

A backup can be made without writing firmware:

~~~sh
python apps/orcsdr-lilygo-p4/tools/lilygo.py backup
~~~

Or specify --port <PORT>. Flash-capacity detection is required to create a size-verified full-chip backup. Backups include private NVS/device information: **do not publish or share them**.

Once you have identified the **AMOLED + SX1262** hardware and know how to recover it, you may deliberately enable an experimental write:

~~~sh
python apps/orcsdr-lilygo-p4/tools/lilygo.py flash --experimental
~~~

The helper identifies the ESP32-P4 revision, builds the matching image, backs up the entire detected flash, verifies backup size, and then invokes idf.py flash. The image remains **headless**. Optional --monitor opens the console after flashing; --no-backup skips backup ONLY when you already have a trusted recovery image. Supply --port <PORT> when more than one USB serial candidate exists.

To monitor an existing matching build without writing flash:

~~~sh
python apps/orcsdr-lilygo-p4/tools/lilygo.py monitor
~~~

**Do not** flash the Tab5 release, the Hosyond panel image, or a v1.x bootloader to a v3.x board. Do not assume the web installer supports LilyGO.

## Software architecture

The application owns **only** its project entry point, configuration overlays and operator CLI. It intentionally reuses the existing P4 console's ESP-IDF main component and component manifest (currently stored at apps/orcsdr-p4-hosyond-console/main), and the shared libraries:

- components/orcsdr_board_hosyond — independent board profiles (includes LilyGO's documentary pin map).
- components/orcsdr_gfx_compat — portable scaled graphics compatibility.
- components/orcsdr_audio_dsp — reusable demodulation.
- esp_rtl_sdr — pinned external clean-room USB receiver driver.
- apps/orcsdr-tab5/ui and experiments/orcui — shared radio/dashboard and UI drawing code.

No copied DSP, RTL driver, or UI fork was created. The existing component directory name reflects its original hardware; rename/extract later after regression testing instead of performing an unrelated risky source move. The console entry point explicitly disables unverified LilyGO peripherals before they can start.

## Remaining milestones

Implement and hardware-validate RM69A10 DSI + GT9895 touch, XL9535 controls, safe USB PHY/host VBUS, C6 firmware/reset, ES8311 codec routing, SD power and SX1262 receive first. Only then enable full services, qualify cold/warm boot and RF, and publish ready-to-flash binaries.

Reference: docs/hardware/P4_BOARD_PROFILES.md and docs/hardware/LILYGO_SX1262_TX_LAB.md in the development branch.
