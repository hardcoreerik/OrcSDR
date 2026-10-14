# OrcSDR graphics port — REA evidence ledger (2026-10-14)

**STATUS: static source review and host-test prototype ONLY.**
No REA/Ghidra binary investigation, IDF build, device flash or RF/graphics
measurements have been performed yet.

## Pinned investigation inputs

- Waveshare 4B example:
  https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-4B
- BSP source inspected at Waveshare-ESP32-components commit
  23a6012d5e87d27399cf90fb851cab3689e29159:
  https://github.com/waveshareteam/Waveshare-ESP32-components/blob/master/bsp/esp32_p4_wifi6_touch_lcd_4b/esp32_p4_wifi6_touch_lcd_4b.c
- Published BSP is waveshare/esp32_p4_wifi6_touch_lcd_4b version 3.0.1,
  esp_lvgl_adapter 0.6.3, LVGL 9.3.0. A live branch checkout may differ from
  the Registry package. Verify the *actual managed-component source and lockfile*
  before relying on a particular call path.
- Tab5 OrcSDR baseline 90d5162; pinned M5Unified 0.2.20 / M5GFX 0.2.27.
  Its pageflip patch is board-specific.
- Tab5 hardware documentation names BMI270 6-axis IMU:
  https://docs.m5stack.com/en/core/Tab5
  M5Stack's IMU example requires M5Unified 0.2.23 / M5GFX 0.2.30+:
  https://docs.m5stack.com/en/arduino/m5tab5/imu

## Source call chain (source-proven, NOT runtime-verified)

| Entry | Actual behavior / porting risk |
|---|---|
| bsp_display_start | configures rotation zero, LVGL adapter and triple-partial mode |
| bsp_display_start_with_config | starts LVGL adapter, LCD, GT911 touch |
| bsp_display_new_with_handles | powers DSI PHY, creates two-lane DSI, ST7703 720x720 |
| bsp_touch_new | probes GT911 primary/backup I2C addresses |
| bsp_display_rotate | forwards to LVGL display rotation; touch mapping must be bench-tested |
| bsp_display_lock/unlock | guards LVGL mutations |
| bsp_usb_host_start | calls usb_host_install: MUST NOT compete with esp-rtl-sdr USB host ownership |

Tab5 source uses direct M5.Display calls, M5Canvas and hard-coded 1280x720
coordinates. Splash restricts orientation to 1/3; Settings currently offers
only LANDSCAPE and LANDSCAPE 180. RF visualizer canvas orientation tracks the
display; framebuffer/pageflip geometry is a separate gate.

**New critical risk**: Espressif documents a PPA freeze on ESP32-P4 with
ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL plus 90/270-degree rotation
in affected ESP-IDF 5.5 revisions. Verify presence of the documented
0001-bugfix-lcd-Fixed-PPA-freeze.patch fix in the actual IDF 5.5.4 tree.
Do not patch managed SDK files ad hoc; document and pin any necessary patch.
Source: https://docs.espressif.com/projects/esp-iot-solution/en/latest/display/tools/esp_lvgl_adapter.html

## Architecture decision

- Shared dashboard/view models, radio controls, decoder and layout geometry.
- Tab5 uses M5GFX, Waveshare 4B uses native BSP + LVGL, Headless uses HTTP.
- Layout: automatic Wide (1280x720), Tall (720x1280), Square (720x720)
  according to post-rotation dimensions. Style can be Balanced,
  Waterfall-Focus or Spectrum-Focus independent of the hardware choice.
- Rotation: Manual, Auto or Locked. Manual rotation has 4 quarter-turn
  possibilities. Calibrate BMI270 axes before enabling auto. 1250 ms
  stable-gravity debounce; reject flat/diagonal/accelerating readings.
- Prototype C++ geometry and motion policy is under
  components/orcsdr_ui_layout, with host tests; NOT connected to firmware yet.

Do not emulate all M5GFX primitives on Waveshare or rewrite working Tab5 in
LVGL. Extract shared dashboard state, then port individual rendering backends.

## REA qualification procedure

1. Capture exact component revisions, hashes, license and build configuration.
2. Run the read-only tools/hardware/rea_graphics_source_audit.py on the
   pinned vendor BSP and original Tab5 UI source. Retain JSON, SHA-256 and log.
3. Native ESP-IDF 5.5.4 build of the isolated Waveshare probe: save lockfile,
   P4 silicon revision and exact ELF/SHA. No build claimed yet.
4. Reuse OrcSDR established REA 6.4.0/Ghidra 12.1.4 isolated workflow,
   .github/workflows/sstv-rea-analysis.yml, against our OWN compiled ELF.
   Save Evidence v2, call graph, logs and diagnostics. NOT YET RUN HERE.
5. Hardware test panel, four rotation angles, GT911 corner/multitouch,
   60-minute render soak, measured DMA/PSRAM usage, Wi-Fi/audio and real IQ.
6. Compare source and physical evidence; write OrcSDR-specific rendering;
   honor vendor licensing, no proprietary-source transplant.
7. Gate browser flashing on real board validation and image provenance.

Memory arithmetic (not measured): RGB565 raw 1280x720 or 720x1280 is
1,843,200 bytes. RGB565 720x720 is 1,036,800 bytes. Triple-partial does
not imply three full screen buffers.

Tab5 testing: preserve existing NVS rotation, calibrate BMI270 x/y/z in four
edge-down positions; verify M5.Imu works on pinned libraries. Then implement
screen_controller-owned atomic graphics rotation, touch remapping, gesture
cancel, viewport recalculation and full redraw. Default current manual
landscape until user opts in. Test RF visualizer, keyboard, splash, settings,
each decoder/dashboard and uninterrupted radio/audio during flips.

**Gates completed**: static vendor/source review and portable C++ tests.
**Gates NOT completed**: binary REA, native IDF compile, physical rotations,
RF stream and webflasher release.
