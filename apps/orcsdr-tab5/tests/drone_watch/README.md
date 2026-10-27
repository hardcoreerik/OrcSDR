# Drone Watch W01 host test

**Scope:** compiles the actual W01 model, actual M5GFX view and actual ScreenController against a minimal **host-only** drawing recorder. The recorded display verifies one structural clear on entry and no additional full-screen clears on 100 unchanged time ticks, touch overlays or source-status updates. No ESP-IDF, physical RF/BLE reception or Tab5 framebuffer performance is implied.

## How to build and run

From the repository root:

```powershell
cmake -S apps/orcsdr-tab5/tests/drone_watch -B build-drone-watch-tests
cmake --build build-drone-watch-tests --config Debug
ctest --test-dir build-drone-watch-tests -C Debug --output-on-failure *>&1 |
  Tee-Object -FilePath .\drone-watch-host.log
```

On Linux/macOS:

```sh
cmake -S apps/orcsdr-tab5/tests/drone_watch -B build-drone-watch-tests
cmake --build build-drone-watch-tests -j
ctest --test-dir build-drone-watch-tests --output-on-failure | tee drone-watch-host.log
```

The `drone-watch-host.log` file is saved to the folder **from which you invoke** the command. Confirm exit codes individually and do not interpret a pipeline success if the test failed. Then run the normal OrcSDR ESP-IDF build using the repo's current pinned toolchain, **without flashing** until device-specific permission.

## Native device acceptance still required

Check Home -> DRONE WATCH from recent and all-dashboard picker, source unavailable copy, SOURCES and HELP, shared header volume/Home/Settings, switching to existing screens, Settings return, 1280×720 contrast/touch, and 720×720 when an supported P4 display integration exists. Measure screen changes, memory, USB overrun and application IQ drops before claiming Hardware-Verified.

No C6 Hosted BLE/Wi-Fi advertisement or OpenDroneID decoder has been integrated. Zero packets doesn't mean zero drones. Other category labels remain disabled/planned.
