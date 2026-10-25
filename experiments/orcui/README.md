# OrcUI — adaptive display prototype

**Status:** Experimental. The geometry + RGB565 raster code has passed isolated host tests in a Linux sandbox. The LVGL 9 Home view is source-only, not yet built or flashed on ESP32-P4. The production Tab5 app and existing M5GFX/USB/DSP components remain unchanged.

## Why this exists

OrcSDR needs one dashboard implementation independent of panel dimensions and vendor graphics libraries. The pure `plan(Display)` function selects rectangle geometry for the Home header, spectrum, waterfall, frequency control, nav drawer, buttons, status, and optional inspector. A conservative inscribed safe square makes rectangular widgets usable on round panels. The LVGL 9 shell uses those rectangles and emits callback commands rather than controlling the SDR directly.

## Run / test

From the OrcSDR repo root, with CMake and C++17 installed:

```bash
cmake -S experiments/orcui/tests -B build-orcui-host
cmake --build build-orcui-host --config Release
ctest --test-dir build-orcui-host -C Release --output-on-failure
```

Or execute `build-orcui-host/orcui_layout_test` directly; it prints PASS summaries for 1280×720, 720×720, 480×480 round, 360×360 round, 2560×720, 480×800, 720×1280, and 800×480. It also sweeps hundreds of other dimensions and checks nesting/nonoverlap, safe insets and RGB565 stride/padding. This is not a test of firmware performance.

## ESP-IDF experimentation

`experiments/orcui/CMakeLists.txt` is an **opt-in** ESP-IDF component. A separate sample application must add that directory to `EXTRA_COMPONENT_DIRS` and install a compatible LVGL 9 version; this branch intentionally does not add LVGL to `apps/orcsdr-tab5`.

1. Start with the official Waveshare 720×720 LVGL v9 example using a pinned BSP/adapter combination. Its current example specifies LVGL 9.3.0 and `esp_lvgl_adapter` 0.6.3. The newer upstream adapter currently requires IDF >=5.5.5, whereas production OrcSDR uses 5.5.4.
2. Provide `Display{actual_width,actual_height,shape,insets,dpi}` from the hardware adapter and call `HomeLvgl::create(lv_screen_active(),display,sink,context)` on the LVGL UI thread.
3. Feed `HomeData` state and bounded FFT frames to `update()`, `spectrum()`, and `waterfall_line()`. Do **not** draw or allocate memory in the USB IQ callback. Arrange a single display/framebuffer owner and test PSRAM, DMA and cache-sync handling.
4. Map `HomeCommand` to existing `home::ActionKind` via an adapter. The research prototype drawer buttons currently only dismiss the menu and are **not** connected to the actual dashboard registry. Production feature parity is still pending.
5. Repeat on Tab5 while retaining its qualified M5GFX baseline. Record rendering/flush p50/p95, frame rate, PSRAM and DMA heap, USB/decoder/audio drop counts, touch latency and tearing on identical workloads.
6. Do not replace Tab5 Home or migrate other dashboards until passing actual hardware gates.

## Current limitations

- No native ESP-IDF/LVGL build or hardware validation. A compile against mock LVGL signatures, if performed, is not a real LVGL build.
- This prototype is not functionally equivalent to OrcSDR Home. Shared menus, navigation routing, gain/filter panels, palette, audio controls, live RF marker overlays and keyboard accessibility are pending.
- RGB565 rendering does full rectangular invalidation for spectrum and shifts the complete waterfall image per line: correct as a bounded baseline, not yet optimized. Benchmark ring buffers/tiled updates/PPA if real profiling proves beneficial.
- Round shape uses an inscribed square rather than contour-aware layout; safe but conservative.
- The current resize operation destroys/recreates LVGL objects; ensure the UI task owns the call and no display flush is in flight.
- Firmware-owned device services (USB power, ESP-Hosted networking, touch drivers, audio, battery) require independent board capability interfaces.

## Sources

- LVGL 9.3 canvas API: https://lvgl.io/docs/open/9.3/details/widgets/canvas.html
- Waveshare ESP32-P4 720×720 example: https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-4B/tree/main/examples/esp-idf/lvgl-v9
- Espressif LVGL port: https://components.espressif.com/components/espressif/esp_lvgl_port
- Espressif LVGL adapter: https://github.com/espressif/esp-iot-solution/tree/master/components/display/tools/esp_lvgl_adapter

**Architecture rule:** Dashboards own state and commands, adaptive layout owns geometry, LVGL owns widgets and input, RF rasters own live pixels, BSP adapters own hardware.
