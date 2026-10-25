# ORCUI-LOCAL-002 isolated integration

Base: `da94e5e76b2424d36903d36e806b7eb54a5857d8` on
`codex/orcui-adaptive-display-poc`. The app tree was imported from exactly
`557d4bec35d625f4bb50636b0454dff979f54d46` on
`codex/waveshare-4b-fm-dashboard`. The original app, badge, FFT, radio queue,
Home v3 and FM IQ-preview sources are retained for the legacy baseline.

The board UI binding is `main/orcui_4b_adapter.*`; `screen_capture.*` supplies the on-demand serial PNG service. The adapter reads LVGL's actual
panel dimensions, supplies immutable receiver snapshots and explicitly aligned
PSRAM canvas allocations, and maps three actions to the existing bounded radio
queue. `orcui_command_map.hpp` makes those mappings host-testable. Palette colors
come from the existing Tab5 `waterfall_style.cpp`, unchanged. No Tab5, C6 or
receiver driver source is modified.

`experiments/orcui/layout.cpp` owns adaptive Home geometry, including scrollable
drawer rows; `home_lvgl.cpp` owns the shared widgets and modal input shield.
The badge is decoded from the original little-endian RGB565 asset into native
pixels. Unsupported routes are visibly unavailable. FM IQ preview remains
available only in the legacy build pending separate adaptive route qualification.
Round profiles reach tune, step and palette through the scrolling drawer.

## Selection and build

`CONFIG_ORCUI_ADAPTIVE_HOME=n` selects **LEGACY_HOME_V3** by default.
`CONFIG_ORCUI_ADAPTIVE_HOME=y` selects **ADAPTIVE_ORCUI_HOME** and excludes both
legacy renderer implementations from compilation. The receiver loop is unchanged
except for selecting the UI consumer and excluding the legacy FM range limiter
when no FM route is enabled.

From this app, in the pinned ESP-IDF 5.5.4 native environment:

```powershell
idf.py -B build-orcui-4b -D IDF_TARGET=esp32p4 -D SDKCONFIG=build-orcui-4b/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.rev1_x;sdkconfig.defaults.orcui" build
```

For a fresh legacy build omit `sdkconfig.defaults.orcui` and choose a separate
build directory. Do not reuse a generated adaptive sdkconfig as a legacy default.
Pinned dependencies: BSP 3.0.1, LVGL 9.3.0, adapter 0.6.3, driver commit
`31a159df4f006a837d5041029bc6d1bc63520234`. Generated lockfile is included.

## Ownership and lifetime

BSP adapter 0.6.3 is the single DSI/framebuffer owner. BSP defaults use triple
partial buffering, no PPA and no touch transform changes. LVGL draws the CPU
RGB565 canvas into its draw buffer; the board adapter never passes canvas memory
directly to DSI/DMA. It relies on the pinned BSP/adapter flush/cache path, rather
than starting a second framebuffer writer or asynchronous canvas DMA operation.
Creation is under the BSP lock; updates and actions run in the LVGL timer/task.
There is no drawing or waiting for UI in the radio/USB/IQ path. Canvas pointers
stay stable on drawer toggles. Native UI teardown/resize is not performed while
streaming; callers of the shared resize API must quiesce in-flight rendering
before teardown. Allocation failure cleans up all retained pointers.

The RF rasters are **relative FFT intensity**, not calibrated dBm/dBFS. No RF
samples are synthesized on the native adapter. Loss of receiver state clears
both plots; battery, WLAN, audio and RDS are never fabricated. Render metrics are
CPU update durations (not panel presentation latency or measured FPS), a rolling
window of up to 40 updates, with DMA minimum and PSRAM free bytes.

## Evidence and open gates

Host suite supports an optional real LVGL 9.3.0 source directory:

```sh
cmake -S experiments/orcui/tests -B build-orcui-host -DORCUI_LVGL_SOURCE=/path/to/lvgl-9.3.0
cmake --build build-orcui-host -j8
ctest --test-dir build-orcui-host --output-on-failure
```

Without that directory only geometry/raster coverage runs; do not claim LVGL
input/lifecycle coverage from that target. With it, tests exercise all six required
sizes, 44px primary targets, actual pointer events, 300 outside dismissals without
radio click-through, HOME/badge dismissal, round-menu scroll reachability,
queue action mapping, stable plot allocations, rectangular-to-round resize and
allocation failure. The PPM emitted by the LVGL test is synthetic host evidence,
not a board photograph.

Local command output/artifacts are retained in the isolated worktree root's
`validation-002/` and dated `orcui-002-*.log` files, with native build/environment
logs in this app's invocation directory. Source and boot/build/physical/RF gates
are separate. The baseline short passive capture at 96.1 MHz reported approximately
960025 samples/s and zero drops. Legacy rollback binary is preserved externally:
`waveshare-4b-home-v3/apps/orcsdr-waveshare-4b/build-4b-home-starter/`, SHA256
`f4186fd79d12a4b15d34c6973580d525b37f7fb733ce0db89e472673fea12361`.

Physical appearance, 50 owner touches, adaptive five-minute RF continuity,
DMA reserve under load and A/B render timing remain unqualified until the exact
built image is flashed and observed. No partition migration, NVS erase, Tab5/C6
flash, FM audio/RDS or physical round-display support is part of this evidence.


## Repeatable device screenshots

See `tools/hardware/WAVESHARE_SCREENSHOTS.md`. After each flash the local agent
captures Home and drawer itself with the serial helper and inspects the actual
firmware-rendered PNGs. The PC saves timestamped images and running-ELF provenance
under `artifacts/screenshots/waveshare-4b/`. This is also available on demand at
any time; no reboot, SD card or HTTP server is needed. User standing approval
covers verified Waveshare application flashes for this ongoing goal, with target
identity/partition/hash checks retained and no erases or Tab5/C6 writes.
