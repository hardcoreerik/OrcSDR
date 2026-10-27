# W01 Live Watch — first implementation checkpoint

**Date:** 2026-10-27 UTC (2026-10-26 PT)  
**Branch:** `chatgpt/drone-watch-phasing-20261026` in `hardcoreerik/OrcSDR` (the original Drone Watch planning branch).  
**Status:** `IMPLEMENTED + HOST-TESTED` for **W01 source-unavailable / no-receiver UI only**. **NOT** `Build-Verified` for native ESP-IDF, `Hardware-Verified`, or `RF-Verified`.  
**Reference:** [W01 screen specification](../screens/watch/W01_LIVE_WATCH.md), [OrcSDR UI rendering requirements](../../UI_RENDERING_STANDARD.md).

## Actual implementation

- `apps/orcsdr-tab5/ui/drone_watch_model.{hpp,cpp}`: host-buildable source state and unambiguous word choice. A silicon component merely being present cannot make `SCANNING` true. Recent validated packets age out without declaring airspace clear.
- `apps/orcsdr-tab5/ui/drone_watch_dashboard.{hpp,cpp}`: M5GFX renderer for W01, large first-state card, receiver-capability pane, recent-activity placeholder, SOURCES/HELP controls, and disabled future-category labels. Has a second compact layout for 720×720; the actual adaptive shared UI/header port is outside this slice.
- `apps/orcsdr-tab5/ui/dashboard_registry.*`: adds one `drone_watch` descriptor and preserves existing ordinal values by appending the new ID before `count`. Registry self-check expects 20 active entries.
- `apps/orcsdr-tab5/ui/screen_controller.*`: adds exactly one `screens::Id::drone_watch`; never a separate framebuffer owner per internal category.
- `apps/orcsdr-tab5/ui/main.cpp`: Home and authenticated serial opening, canonical shared header, app entry/exit, Settings/Visualizer return, documentation return, header/touch dispatch, idle tick and suppression of unrelated stream transition redraw. **Does not** retune, claim C6 Bluetooth capture, change Wi-Fi, flash firmware, start USB streaming or write SD just to open W01.
- `apps/orcsdr-tab5/main/CMakeLists.txt`: registers the two new UI/model translation units.

## Recorded validation

[**GitHub Actions W01 host CI — PASS**](https://github.com/hardcoreerik/OrcSDR/actions/runs/40459967209), job `w01-model-and-render`, commit `9bd2990386b268d9363074b0eb135d3bb4b6759f`.

```text
cmake -S apps/orcsdr-tab5/tests/drone_watch -B build-drone-watch-tests
cmake --build build-drone-watch-tests --parallel
ctest --test-dir build-drone-watch-tests --output-on-failure
[100%] Built target drone_watch_host_test
1/1 Test #1: drone_watch_host ... Passed
100% tests passed, 0 tests failed out of 1
```

This compiles the **actual** renderer and model using `mocks/M5Unified.h`, plus OrcSDR's real `screen_controller.cpp`. It uses C++17 `-Wall -Wextra -Wpedantic -Werror` on the Ubuntu runner. Test assertions cover no qualified capture in default state, unqualified silicon, ready/paused/degraded/error/unsupported/listening states, replay `TEST DATA`, simulated validated packet expiry, source/help touch, screen ownership, no display writes off-screen, a structural clear on entry but **no repeat full-screen clear** during 100 time ticks or source/status changes, and compact square Home touch action. No full-image pixel-golden comparison or physical 5-inch readability has been performed.

An intermediate warning-as-error job [failed](https://github.com/hardcoreerik/OrcSDR/actions/runs/40459881530) after a test-source replacement introduced literal `\\n` characters. The test fixture formatting was corrected and the final linked W01 CI run passed. Do not treat the initial failure as a real product build failure; preserve its record here.

## Remaining independent acceptance gates

| Gate | State | Required next work |
| --- | --- | --- |
| C++17 host source/model/render | PASS | Link above; repeat on merged/final SHA |
| API/controller registration | Source-reviewed / host registry build previously in existing CI; native route NOT RUN | Verify on actual Tab5 including dashboard picker/recent, serial OPEN, Settings and Visualizer return |
| Native ESP-IDF 5.5.4 compile/link | NOT RUN | Compile from current pinned repo environment with exact `esp_rtl_sdr` dependency and save full build log/image hash |
| 1280×720 5-inch physical drawing/touch | NOT RUN | Flash specifically approved board; verify contrast, touch target, no flicker, overlays, memory and IQ continuity |
| 720×720 physical/adaptive header | NOT RUN | Validate real display and input driver, not merely host width=720 layout |
| ESP32-C6 Remote ID BLE/Wi-Fi acquisition | NOT IMPLEMENTED | C6 Hosted HCI feasibility and real authorized packets; no firmware update without approval |
| OpenDroneID C decoder/parser | NOT IMPLEMENTED | Separate P1 conformance and malformed-input fixtures |
| Other 24 screens and receiver-specific features | NOT IMPLEMENTED | Follow `docs/drone-watch/SCREENS.md` independently |
| Full OrcSDR release regression | NOT RUN | FM/RDS, ADS-B, LoRa, Wi-Fi, SD, maps, Dead Air and USB IQ counters |

## How to run/test locally (Windows)

In your checked-out OrcSDR worktree for the **existing branch**:

```powershell
git fetch origin
git switch chatgpt/drone-watch-phasing-20261026
git pull --ff-only
Set-Location apps/orcsdr-tab5
Set-Location ../..

cmake -S apps/orcsdr-tab5/tests/drone_watch -B build-drone-watch-tests
cmake --build build-drone-watch-tests --config Debug
ctest --test-dir build-drone-watch-tests -C Debug --output-on-failure *>&1 |
  Tee-Object -FilePath .\drone-watch-host.log
```

The log is created in the folder from which the command is run. Check `$LASTEXITCODE`, and read `apps/orcsdr-tab5/tests/drone_watch/README.md` for the host harness and additional physical acceptance. To attempt a native build, inspect the maintained `apps/orcsdr-tab5/tools/build-tab5-idf.ps1` and use its required IDF environment; do **not** flash any device without explicit device authorization.

## Known limitations

The default production view says `SOURCE UNAVAILABLE`; it is an honest shell, **not a working Remote ID receiver**. UI contains only W01 and two functional in-page actions; remaining category controls are muted as planned. No live aircraft/position/event list exists yet. Source status injection was used in *host tests only*; no synthetic stream is seeded in production. The first phase must not be called a complete Drone Watch dashboard release.
