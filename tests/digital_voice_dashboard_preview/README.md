# Digital Voice UI-DV1 host regression

The production renderer uses M5GFX through `<M5Unified.h>` and an application-owned framebuffer. These host tests substitute a **command-recording mock M5Unified display**, which validates clipping/geometry, text calls, evidence gates, UI action boundaries, six tabs × two density modes, scanner controls, NXDN RAN/channel/topology, Last Heard paging and ScreenController surface ownership. A passing test is not a pixel-perfect M5GFX screenshot or Tab5 performance measurement.

## Run / test

From the **OrcSDR repository root** (the commands save logs in the directory from which you run them):

```sh
cmake -S tests/digital_voice_dashboard_preview -B build/dv-ui-host -DCMAKE_BUILD_TYPE=Release 2>&1 | tee ./dv-ui-configure.log
cmake --build build/dv-ui-host --parallel 2 2>&1 | tee ./dv-ui-build.log
ctest --test-dir build/dv-ui-host --output-on-failure -V 2>&1 | tee ./dv-ui-test.log

cmake -S tests/digital_voice_dashboard_preview -B build/dv-ui-asan -DCMAKE_BUILD_TYPE=Debug -DORC_DV_UI_SANITIZERS=ON 2>&1 | tee ./dv-ui-asan-configure.log
cmake --build build/dv-ui-asan --parallel 2 2>&1 | tee ./dv-ui-asan-build.log
ctest --test-dir build/dv-ui-asan --output-on-failure -V 2>&1 | tee ./dv-ui-asan-test.log
```

For PowerShell, run the equivalent CMake commands and pipe with `Tee-Object -FilePath .\dv-ui-test.log`. The output directory is ignored by standard build practices; logs are operator artifacts rather than source files.

## Native-device acceptance still required

Use OrcSDR's firmware build tool and M5Unified/M5GFX on a Tab5 to verify header coexistence, exact 1280×720 native framebuffer captures, pixel-repeatability, touch mapping, overlay restoration, memory/PSRAM pressure, UI redraw cost and IQ continuity. Capture the actual M5GFX image; never substitute AI-generated mockups for hardware screenshots.
