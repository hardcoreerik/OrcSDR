# OrcUI Desktop Simulator (LVGL 9 + SDL2)

**Experimental.** This is a native desktop application using the same `home_lvgl.cpp`, `layout.cpp` and `raster.cpp` as the Waveshare 4B experimental firmware. It uses synthetic FFT input, never claims to receive real RF, and never opens an SDR device.

**Validation:** GitHub Actions has now compiled and run the native LVGL 9.3/SDL2 Linux simulator successfully. The cloud sandbox itself lacks LVGL sources and SDL dependencies, and no Windows PC installation or physical P4 hardware test has been verified here. Local Codex should complete Windows installation and record evidence.

## Profiles

Use the SIM toolbar's display dropdown to switch live without running separate dashboards:

- `waveshare` — 720×720
- `tab5` — 1280×720
- `round480` — 480×480
- `round360` — 360×360
- `ultrawide` — 2560×720
- `portrait` — 480×800
- `compact` — 800×480

The dropdown resizes the SDL LVGL display and calls `HomeLvgl::resize` on the **same** UI implementation. Round mode currently uses a safe inscribed square; the SDL window is not physically masked to a circle.

## Windows (PowerShell)

From your **local OrcSDR checkout**, after switching to `codex/orcui-adaptive-display-poc`:

```powershell
cd experiments/orcui/desktop
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\install-windows.ps1
```

Installs MSYS2 when needed, UCRT64 GCC/CMake/Ninja/SDL2, fetches upstream LVGL **v9.3.0** by tag, compiles OrcUI, runs the two-second SDL headless smoke test, then opens the GUI. A nonzero test or build exit is a failure, not a successful installation. Does not change ESP-IDF or flash firmware.

Launch an alternate layout later:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-windows.ps1 -Preset tab5
```

Windows installation and launch logs live in this folder as `install-windows.log` and `orcui_desktop.log`. If MSYS2 requests a package-manager restart, close/reopen its shell, run `pacman -Syu`, then retry installation.

## Linux

Install GCC/G++, CMake, Ninja, SDL2 development files, pkg-config and git, then:

```bash
cmake -S experiments/orcui/desktop -B experiments/orcui/desktop/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build experiments/orcui/desktop/build --parallel 4
ctest --test-dir experiments/orcui/desktop/build --output-on-failure
experiments/orcui/desktop/build/orcui_desktop --preset waveshare
```

For offline testing with an existing LVGL checkout, set `-DORCUI_LVGL_SOURCE=/absolute/path/to/lvgl`. For CI, run `SDL_VIDEODRIVER=dummy orcui_desktop --seconds 4 --cycle-ms 500`.

## Actual SDL screenshot capture

Screenshots are read back from the **real LVGL-rendered SDL surface**, not produced from mockup assets. Use the CLI after building:

```powershell
.\build\orcui_desktop.exe --preset waveshare --seconds 2 --screenshot waveshare.bmp
.\build\orcui_desktop.exe --preset tab5 --seconds 2 --screenshot tab5.bmp
```

On Windows, launch through the UCRT64 environment or set `C:\msys64\ucrt64\bin` on `PATH` so the SDL2 DLL can load. If a display profile uses an SDL zoom factor, screenshot pixel dimensions match the **physical SDL window**, rather than the logical screen coordinates. The GitHub Actions CI stores BMP captures for Waveshare, Tab5, round480 and ultrawide under its run artifacts.

## Acceptance

1. Build succeeds and CTest returns 0.
2. Home shows moving synthetic spectrum and waterfall.
3. Switch between every profile using SIM toolbar without a crash or clipped primary controls.
4. Click MENU and MODE/FILTER/GAIN/SPAN buttons; observe console command logs. Pause/resume data updates.
5. Run 15-minute soak; report peak memory, exceptions, image distortion, and any UI crashes.
6. Screenshot and log results to OrcUI branch. Only after this should we integrate live radio state or change Tab5 firmware.

The simulator does **not** validate ESP32-P4 DMA/cache, DSI throughput, physical touch latency, USB RF streaming, or the production M5GFX framebuffer path. Menu destinations are currently placeholders.
