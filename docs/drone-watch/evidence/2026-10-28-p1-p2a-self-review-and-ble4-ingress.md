# Drone Watch P1/P2a self-review — decoder, BLE4 ingress, C6 Hosted gates

> **Date:** October 28, 2026 Pacific / October 29 UTC.  
> **Work branch:** `chatgpt/drone-watch-phasing-20261026`, same existing branch and draft PR #213; **no new OrcSDR branch**.  
> **Software evidence:** [GitHub Actions run 40829468655](https://github.com/hardcoreerik/OrcSDR/actions/runs/40829468655): **6/6 normal CTest PASS and 6/6 ASan/UBSan CTest PASS**, including W01 rendering, C6 gate, ASTM BLE4 envelope, application decoder, bounded store and ingress pipeline.  
> **Native ESP-IDF compile / flashing / actual ESP32-C6 BLE RF / on-device coexistence:** **NOT RUN**. Do not reclassify the host pass as RF-Verified.

## Review method, sources and authority

Reviewed current W01/decoder/store source against [Drone Watch UI/UX](../UI_UX_STANDARD.md), [data contracts](../DATA_CONTRACTS.md), [navigation](../NAVIGATION.md), [rendering acceptance](../RENDERING_AND_ACCEPTANCE.md), Tab5's actual `dashboard_audio_control.*` and `screen_controller.*`, current `wifi_service.{cpp,hpp}`, `apps/orcsdr-tab5/sdkconfig.defaults`, `main/idf_component.yml`, [Tab5 Wi-Fi guide](../../user-guide/wifi-tab5.md), and [C6 Hosted compatibility findings](../../validation/esp-hosted-c6-compatibility-2026-09-29.md).

**Documentation-reference issue:** `docs/UI_RENDERING_STANDARD.md` and `apps/orcsdr-tab5/ui/AGENTS.md` are referenced by the proposed Drone Watch UI specs but are **not present on this branch** (GitHub 404 during this review). No authority was invented in their place; use the actual shared header/ScreenController code and Drone Watch's checked-in UI/UX, data and rendering specifications. Resolve canonical references in a separate docs hygiene change; do not refactor main UI in a radio protocol PR.

Public authoritative research:
- [Espressif ESP-Hosted 3.0.6 NimBLE Wi-Fi coexistence example](https://components.espressif.com/components/espressif/esp_hosted/versions/3.0.6/examples/bluetooth/esp_hosted_nimble/bleprph_wifi_coex/mcu_host): ESP32-P4 / ESP32-C6, Hosted HCI over SDIO available in specifically configured host **and co-processor** builds; not evidence for installed OrcSDR C6 image.
- [Espressif Hosted Bluetooth feature guide](https://github.com/espressif/esp-hosted-mcu/blob/main/docs/features/bluetooth.md): configure `CONFIG_ESP_HOSTED_HOST_FEAT_BT`, select a host stack and call its explicit setup only after the transport/firmware build matches.
- [Official OpenDroneID transmitter Bluetooth 4 implementation](https://github.com/opendroneid/transmitter-linux/blob/main/bluetooth.c), inspected Git blob `8a29d70318ac4db8651d36668539263dd41248c1`: GAP AD len 0x1E, type 0x16, 16-bit ASTM UUID 0xFFFA (FA FF), application code 0x0D, one-byte message counter and exactly 25 encoded ODID bytes.
- [Official pinned decoder](https://github.com/opendroneid/opendroneid-core-c/tree/849c03adbe0cdd68997a462fafab397edcfe53f3) Apache-2.0. The .c/.h/license blobs already matched exactly in the first P1 checkpoint.

## Findings and dispositions (not guessed)

| Severity | Finding | Corrective work | Current verification |
| --- | --- | --- | --- |
| **HIGH** | Tab5 current `sdkconfig.defaults` enables Hosted **Wi-Fi** but does **not** enable `CONFIG_ESP_HOSTED_HOST_FEAT_BT` or a P4 BT host stack; Hosted transport "ready" was at risk of being misconstrued as BLE receiver-ready | Pure `drone_watch_c6_probe` + read-only native `drone_watch_c6_native`, W01 SOURCES status: `BLE: HCI BUILD DISABLED`, `BLE: HOST STACK DISABLED`, `BLE: LINK UNAVAILABLE` or `BLE: FIRMWARE UNVERIFIED`; **never green scanning** solely from a Wi-Fi link | Host gate tests the flags/status; device config/native compile NOT RUN |
| **HIGH** | A decoded 25-byte message is **not** itself an extracted BLE advertisement; arbitrary 31-byte AD cannot be passed straight into decoder | New `drone_watch_ble4`: exact AD-TLV bounds/ASTM service UUID/app code/counter framing from official transmitter, excluding HCI-report headers | `drone_watch_ble4_test` PASS; unrelated, malformed, wrong UUID/app and 4,000 seeded random AD cases |
| **MEDIUM** | Prior provenance inferred replay only from `Transport::fixture`; replayed BLE capture could keep BLE transport and erroneously lose TEST DATA marker | Added explicit `CaptureMeta.test_data` independent of `Transport`, carried to batch/event store | Replay with physical transport `bluetooth4` retains `test_data=true`; tests PASS |
| **MEDIUM** | Earlier `EventStore::reset()` reset count/indices while leaving old IDs/coordinates in memory | Reset now clears retained 32-event metadata as well as counters | Host store tests PASS; not a cryptographic memory-erasure guarantee |
| **MEDIUM** | Source inspector had hardcoded C6 statements and could not reflect a Wi-Fi Hosted link status change without confusing it with BLE availability | W01 reads cached Wi-Fi state via read-only UI task hook; bounded 2-second polling updates **only source panel**, no C6 reset/start/scan or full-screen redraw | Host W01 renderer/ownership tests PASS; actual UI/IRQ/display performance NOT RUN |
| **MEDIUM** | BLE counter, source ID and capture timestamp need to survive actual AD extraction without implying a unique transmitter | `drone_watch_ble4_ingress`: extracted counter + capture metadata into 32-event store with distinct counters for unrelated/malformed/invalid/wrong-transport; **no identity fusion** | Full host ingress regression PASS; no hardware queue yet |
| **LOW** | First P1 evidence claimed lack of complete message-kind tests though follow-up CI covered six individual families | Explicitly record 6 message types + pack, 5,000 RID random candidates, 4,000 BLE4 AD candidates, 6/6 normal and 6/6 sanitizer executions | CI log, ref above |
| **OPEN** | Native ESP-IDF and installed C6 Bluetooth firmware capability not proven; past C6 v3.0.6 vs v2.12.6 matters for M5 Launcher compatibility | No Kconfig/C6 firmware changes; stop at read-only probe and wire-format ingestion | NOT RUN; hardware owner approval required |
| **OPEN** | `main.cpp` is very large and native dashboard integration can differ from host-renderer linkage | Minimal include + `Hooks.inspect_c6` call only; use existing ScreenController, shared header and UI task; defer unrelated refactor | Host PASS, native and flash NOT RUN |

### P2a current runtime behavior

On a normal current OrcSDR build, opening W01 **does not activate Bluetooth**, initialize Hosted, scan Wi-Fi, reset the C6, modify C6 firmware, retune the RTL-SDR or write to SD. Its W01 source inspector will report the observed Wi-Fi Hosted transport/link condition **separately** from a Host Bluetooth HCI build gate. Even when the Wi-Fi SDIO link is already up, **BLE remains unqualified**; Wi-Fi Beacon and NAN raw capture remain separately unqualified.

The `drone_watch_c6_native.cpp` adapter reads only the existing `wifi_service` cached status and compile-time Kconfig feature flags. It does not issue bus RPCs. The UI hook polls every two seconds *while the dashboard owns the framebuffer* and repaints only its affected card when status actually changes. Host C6 probe does not prove P4/C6 BLE controller firmware mode or BLE5 extended/coded reception.

### BLE4 reference envelope and ingress

```text
Legacy AD data (31 bytes, excluding HCI report length):
  1E | 16 | FA FF | 0D | counter | 25-byte OpenDroneID payload
  LEN   AD    ASTM   app   1 byte       exactly 25 bytes
        type  UUID    code
```

The 25-byte `drone_watch_rid` parser handles message content separately. `drone_watch_ble4` checks AD-TLV lengths and exact UUID/app, and `drone_watch_ble4_ingress` accepts only explicitly labeled `Transport::bluetooth4`, processes the envelope, then decodes/store-ingests. A valid service UUID is **not** an authenticated aircraft identity. The application-message parser cannot itself verify the actual RF source, and the counter wraps and cannot serve as a global identifier.

**No HCI LE Advertising Report parsing is yet implemented.** A future approved receiver task/stack must normalize actual HCI events into bounded AD data, preserve RSSI only where reported and in correct units, and deliver data to a bounded app-service queue. Do not call UI draw functions from the HCI callback, and do not let a test-source packet update W01 as LIVE.

## Exact automated evidence

[Run 40829468655](https://github.com/hardcoreerik/OrcSDR/actions/runs/40829468655) built the actual OrcSDR parser, vendor C core, W01 renderer/model, BLE4 AD extractor/ingress, C6 gate and bounded store as **host** C/C++ modules. Both regular CTest and ASan/UBSan CTest had:

```text
1/6 drone_watch_host           PASSED
2/6 drone_watch_rid            PASSED
3/6 drone_watch_store          PASSED
4/6 drone_watch_c6_probe       PASSED
5/6 drone_watch_ble4           PASSED
6/6 drone_watch_ble4_ingress   PASSED
100% tests passed, 0 tests failed out of 6
```

C++ host targets compile with warnings treated as errors. The two-stage run also uses `-fsanitize=address,undefined -fno-omit-frame-pointer`. These are **six test executables**, not an estimate of total assertions or coverage percentage. The RF/device/ESP-IDF compile remains untested.

## Next gates (in order, without compromising existing radios)

1. **Native compile before flash:** on an OrcSDR worktree with ESP-IDF 5.5.4 and pinned driver `31a159df4f006a837d5041029bc6d1bc63520234`, build the current branch; save full build log, errors, app size, relevant Kconfig and SHA256. If build fails, fix compiler/linker only; do not apply a C6 firmware upgrade as a workaround.
2. **Existing-C6 read-only test:** after explicit device choice/approval, inspect W01 Sources with C6 idle, Wi-Fi up, disconnected and link failure; verify `BLE HCI BUILD DISABLED`, no fake green scanning and existing Wi-Fi/RTL unchanged. Test both 1280×720 actual screen and adaptive 720×720 when supported. Compare USB/IQ/SD metrics.
3. **Separate C6 Bluetooth feasibility proposal:** identify current C6 firmware's actual controller configuration. Do not treat 3.0.6 Wi-Fi version as a BLE qualification. Only after an owner-approved recovery/Launcher plan, consider a deliberate opt-in HCI/NimBLE build (and any **separately approved** compatible C6 image). Repeatedly verify Wi-Fi/SDIO, Launcher, SD and USB radio.
4. **Actual BLE4 RF capture:** obtain authorized known test broadcaster raw HCI Advertising Report + normalized 31-byte AD, source, monotonic receive time and RSSI if available. Independently decode against a reference receiver. Then install a bounded task handoff and connect W01 live state.
5. **Independent BLE5 extended/coded, Wi-Fi beacon and NAN:** each has a distinct proof gate and transport frame extractor; do not generalize the BLE4 result.
6. **Release-grade acceptance:** real FM/ADS-B/LoRa/Dead Air/OrcMaps, USB drop, DMA heap/PSRAM, Wi-Fi, SD, power and screen regressions with exact build hashes.

No changes were made to `sdkconfig.defaults`, firmware images, C6 OTA flow, `esp_rtl_sdr`, `OrcMaps`, `OrcNode`, SDIO hardware pins, receiver ownership or production radio processing.

## Reproduce locally / logfile in invocation folder

```powershell
git fetch origin
git switch chatgpt/drone-watch-phasing-20261026
git pull --ff-only

cmake -S apps/orcsdr-tab5/tests/drone_watch -B build-drone-watch-tests
cmake --build build-drone-watch-tests --config Debug
ctest --test-dir build-drone-watch-tests -C Debug --output-on-failure *>&1 |
  Tee-Object -FilePath .\drone-watch-host.log
if ($LASTEXITCODE -ne 0) { throw "Host test failure" }

cmake -S apps/orcsdr-tab5/tests/drone_watch -B build-drone-watch-sanitized -DDRONE_WATCH_SANITIZE=ON
cmake --build build-drone-watch-sanitized --config Debug
ctest --test-dir build-drone-watch-sanitized -C Debug --output-on-failure *>&1 |
  Tee-Object -FilePath .\drone-watch-sanitized.log
if ($LASTEXITCODE -ne 0) { throw "Sanitizer regression" }
```

Both logs save in the folder where commands are invoked. The sanitizer mode requires Clang or GCC; on MSVC run the normal host build and rely on the Linux CI sanitizer gate. Check the commands' exit codes and record OS/compiler versions. Only after these pass attempt the repo's maintained native IDF script in a correctly configured environment. **No automatic device flash.**
