# Drone Watch W01 + P1 Remote ID host tests

**Scope:** three independent host executables compile the actual W01 M5GFX renderer/model/ScreenController; OrcSDR's own bounded Remote ID packet decoder against the pinned Apache-2.0 OpenDroneID core; and the fixed-capacity observation history, source/clock provenance and loss counters. The recorded display verifies one structural clear on entry and no additional full-screen clears on 100 unchanged time ticks, touch overlays or source-status updates. No ESP-IDF, physical RF/BLE reception or Tab5 framebuffer performance is implied.

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

The OpenDroneID protocol decoder is now implemented and host-tested, but **no C6 Hosted BLE/Wi-Fi acquisition adapter has been integrated** and no live observations flow to W01. Zero packets doesn't mean zero drones. The remaining category labels stay disabled/planned. Other category labels remain disabled/planned.

## Tests included

- `drone_watch_host`: actual M5GFX W01 source status, no-data, touch, off-screen ownership, no repeated full-screen clear.
- `drone_watch_rid`: reference-derived BasicID/Location/SelfID/System/Auth/OperatorID/pack tests, invalid/unsupported/secret fields, 5,000 deterministic malformed/random candidates and strict packet/version/size limits.
- `drone_watch_store`: 32-entry fixed-size event store, replay flag, independent source IDs, rejection/overflow counters.

All are host-only. They do **not** verify BLE/Wi-Fi capture, timing, complete standards certification, physical RF, native ESP-IDF compilation or an aircraft. The vendored core is byte-identical with pinned upstream SHA `849c03adbe0cdd68997a462fafab397edcfe53f3` and its original license is retained. See [P1 evidence checkpoint](../../../../docs/drone-watch/evidence/2026-10-28-rid-core-host-checkpoint.md).
