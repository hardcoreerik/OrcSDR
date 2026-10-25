# OrcUI Waveshare 4B standalone display/touch smoke test

**Status:** Source ready for **local ESP-IDF compilation**, NOT compiled/flashed on hardware in this cloud sandbox. This app is deliberately separated from production Tab5 firmware. RF spectrum/waterfall samples are **synthetic**; there is no radio receiver, TX, Wi-Fi configuration, or C6 firmware update.

Hardware: Waveshare **ESP32-P4-WIFI6-Touch-LCD-4B**, 720×720.

## Prerequisites

Install ESP-IDF 5.5.x, using its supported Python environment and RISC-V ESP32-P4 cross-toolchain. Connect the Waveshare's P4 flash/serial USB data port. Confirm that port in Device Manager, `idf.py monitor`, or the operating system device list. Back up any existing firmware/data you need to retain: flashing a new P4 application replaces the running P4 application.

This application pins the same Waveshare BSP 3.0.1, esp_lvgl_adapter 0.6.3 and LVGL 9.3.0 dependencies used in the vendor's official LVGL-v9 demo. Dependencies will be downloaded on the building machine. The IDF version constraint is 5.5.x, separate from OrcSDR's production Tab5 lock.

## Windows PowerShell — build and flash

Open an ESP-IDF 5.5.x PowerShell environment, then run from the OrcSDR checkout:

```powershell
cd experiments/orcui/waveshare4b
idf.py set-target esp32p4
idf.py build
idf.py -p COM7 flash monitor
```

Replace `COM7` with the actual **Waveshare ESP32-P4** COM port; it is only a placeholder. Never flash the Tab5 COM port. If the board requires manual download mode, follow Waveshare's hardware instructions. Exit the serial monitor with Ctrl+].

## Linux

```bash
. "$IDF_PATH/export.sh"
cd experiments/orcui/waveshare4b
idf.py set-target esp32p4
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Replace `/dev/ttyACM0` with the verified Waveshare P4 serial device.

## Test procedure

1. Confirm the boot log identifies the **OrcUI-4B SYNTHETIC RF** experiment.
2. Observe OrcSDR Home, a moving synthetic spectrum and a scrolling waterfall.
3. Tap MENU to open the adaptive navigation drawer, then use a drawer control to close it.
4. Tap SPAN-/SPAN+, MODE, FILTER, and GAIN and confirm `touch_command` counters increase in serial logs.
5. Run for at least 15 minutes. Capture the `ui_updates_5s`, `internal_heap`, `psram_heap`, `touch` reports every five seconds; note resets, tearing or dead touch areas.
6. Preserve the build log and flash monitor log in the experiment folder. A simple way to capture a repeatable artifact from PowerShell is `idf.py -p COM7 monitor 2>&1 | Tee-Object -FilePath orcui_waveshare4b_monitor.log` after flashing; stop with Ctrl+].

This test intentionally uses ~10 synthetic spectrum-data updates per second. Those are not measured panel FPS or LVGL flush timings. Do not claim stable display/rendering throughput from that number alone.

## Limitations

- The cloud sandbox did not contain `idf.py`, LVGL dependencies, or a visible USB board. **No embedded compilation or flashing has happened.** SDK/BSP integration errors could remain.
- Canvas memory uses LVGL allocator; PSRAM placement and frame-cache/DMA ownership must be confirmed on target.
- The navigation drawer is demonstrative, not wired to existing OrcSDR app/dashboard switching. Visual controls emit prototype actions, not receiver commands.
- The current RGB565 renderer invalidates full spectrum/waterfall canvas regions. It is a correctness baseline, not optimized for sustained real SDR load.
- The first hardware objective is display and touch proof. Only afterward should live FFT and complete Home action parity be integrated.
- Production Tab5 firmware, its C6 pairing, and its M5GFX page-flip path are unchanged.

Official BSP reference: https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-4B/tree/main/examples/esp-idf/lvgl-v9
