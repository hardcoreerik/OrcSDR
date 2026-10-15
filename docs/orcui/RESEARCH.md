# OrcUI: adaptive display research

**Status:** experimental; not production-ready. This branch is isolated from the shipping M5Stack Tab5 UI and receiver pipeline.

## Objective

OrcSDR should have one dashboard implementation per feature, not one per board or screen resolution. Widgets share state and actions; an adaptive layout policy places them based on display metrics and physical shape. ESP-IDF panel drivers and LVGL 9 are recommended for research, with optimized RGB565 spectrum and waterfall draw paths.

## Proposed boundaries

1. Radio/DSP modules own the receiver, FFT and decoders. UI reads immutable snapshots via bounded queues; drawing never occurs in IQ callback.
2. A graphics-library-neutral display descriptor describes width, height, circle/rectangle shape, safe insets, and capabilities.
3. A pure layout engine produces view rectangles and selection policies for wide, square, portrait/compact, round and ultrawide.
4. LVGL 9 owns buttons, labels, navigation, input dispatch and main frame composition. The spectrum and waterfall draw to caller-owned RGB565 buffers.
5. A single board adapter owns display initialization, panel/touch, framebuffer flush, cache coherence and input orientation. M5GFX remains qualified on the current Tab5 until physical parity tests pass.
6. Existing Home Snapshot/Action conventions become bindings to the shared interface; no duplicate decoder or dashboard logic.

## Research gates

- Build desktop geometry tests at 1280×720, 720×720, 480×480 circular, 360×360 circular, 2560×720, and portrait.
- Test clipped controls, safe areas, state continuity, and hit targets.
- Compile LVGL 9 on a standalone ESP32-P4 sample application with a pinned vendor BSP. Do not alter app's dependency lock simply to experiment.
- Compare current M5GFX Tab5 UI to LVGL under identical USB sample rates and real DSP work. Log spectrum frame times, waterfall update rates, touch latency, heap, USB drops and audio underruns.
- Integrate Home only after successful hardware gates. Avoid any breaking changes to `main.cpp` or current dashboards.

## Compatibility and evidence

OrcSDR currently pins ESP-IDF 5.5.4, M5Unified 0.2.20 and M5GFX 0.2.27. Current `esp_lvgl_adapter` main has an IDF >=5.5.5 requirement; vendor Waveshare 720×720 LVGL 9 demo pins LVGL 9.3.0 and esp_lvgl_adapter 0.6.3. Pin exact compatible components and test, rather than automatically upgrading the production SDK.

Primary source: https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-4B/tree/main/examples/esp-idf/lvgl-v9

LVGL canvas contract: https://lvgl.io/docs/open/9.3/details/widgets/canvas.html

Espressif LVGL port: https://components.espressif.com/components/espressif/esp_lvgl_port

**No ESP-IDF hardware builds or flashes have been performed for this research branch.**
