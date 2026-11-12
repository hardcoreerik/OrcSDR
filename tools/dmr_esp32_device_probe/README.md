# OrcSDR DMR D1-H: ESP32-P4 device qualification probe

**Status: OPT-IN, STANDALONE, NOT WIRED TO TAB5, HARDWARE NOT MEASURED.**

This folder contains a C++17 bounded SPSC IQ handoff and an isolated ESP-IDF
ESP32-P4 synthetic-load image. It compiles the **actual** OrcSDR D0, D1-A
(protocol), D1-B (48k 4FSK frontend), and D1-C (frontend/protocol integration)
sources by reference; no decoder files are copied or changed. No new driver,
second receiver, streaming endpoint, dashboard, audio path, microSD code,
configuration writer, tuner controller, or P25 integration exists here.
The standalone image must be intentionally flashed to a *spare/test* ESP32-P4;
it is **not** an optional feature of the current installed OrcSDR firmware.

## What the repo currently does (read-only source audit)

1. `apps/orcsdr-tab5/ui/main.cpp` owns live sample delivery from the pinned
   `esp_rtl_sdr` driver, including `on_rtl_driver_event` / IQ blocks, borrowed
   driver buffer copying, three 33,280-byte PSRAM IQ slots and
   `rtl_free_q`/`rtl_filled_q`. Existing `rtl_dsp_task` on core 1 is the
   sole decoder/demod dispatcher. `rtl_tcp` feeds the *same* pipeline;
   `rtl_source_routing`/`receiver_source_policy` select the active radio source.
   These details are described in `docs/RTL_TCP_INTEROP.md`; **the monolithic
   `main.cpp` was not modified by this probe**.
2. The `apps/orcsdr-tab5/dsp/multirate.hpp` D2/D3 channelizer is documented as a
   *benchmark lab*, not a live DMR channel feed. Its published outputs are
   240 kS/s complex, not DMR's required 48 kS/s. Do **not** assume a 48k DMR
   subscription already exists. P25's `p25core::process_channel_iq()` is a
   private channel-IQ processing seam, **not** an approved public DMR tap.
3. `components/orcsdr_dmr/d1_frontend/include/frontend.hpp` requires a
   continuous, **centered, filtered, 48,000 complex CF32 samples/s** channel;
   12.5 kHz DMR channel bandwidth and 4,800 dibits/s. It has a 61-tap
   frequency filter, a 1,024-float measured pre-roll history, and private
   clock/phase state. It is **not** an RF-wideband channelizer.
4. `components/orcsdr_dmr/d1_integration/include/orcsdr_dmr/d1_bridge.hpp`
   exposes `integration::Receiver::process()`, `discontinuity()`, and read-only
   stats. The bridge includes the real `Frontend`, D1 `Decoder` and a 256-entry
   sample-to-symbol timestamp map. A DSP callback must not call the slow
   frontend/decoder synchronously if this risks stalling the source pipeline.
5. `apps/orcsdr-tab5/sdkconfig.defaults` targets ESP32-P4 / IDF 5.5.4,
   uses PSRAM, reserves internal DMA memory for USB/Hosted/I2S, and sets a
   1 kHz FreeRTOS tick. `rtl_dsp_task` competes with audio, protocol, map and
   UI workloads. The standalone probe does **not** qualify simultaneous load.

## Safest future integration point

**An opt-in, read-only tap on a 48 kS/s complex DMR channel produced inside
`rtl_dsp_task` by the existing radio service**, after frequency translation
and anti-alias filtering and before audio/DSP effects. The actual selectable
12.5 kHz channelizer needs to be approved, integrated and profiled separately;
this probe deliberately does not create a second DSP architecture or alter
capture ownership to supply it.

```text
existing esp_rtl_sdr or rtl_tcp -> existing CU8 ownership and queues
       -> rtl_dsp_task / existing radio service -> approved 12.5k DDC
                              | 48k CF32, const + epoch, read-only
                              v
                 optional SpscHandoff::offer() (bounded memcpy, no wait)
                              | 8 x 240 IQ blocks / 40ms capacity
                              v
                independent low-priority worker (one consumer)
                              | actual D1 integration::Receiver
                              v
                       counted events/metrics only
```

The producer must never call the D1 receiver or emit files / logs; the worker
must never retune, call USB, modify P25, or block the existing DSP task. Details
and rejection rules are in [`INTEGRATION_CONTRACT.md`](INTEGRATION_CONTRACT.md).

## Host test (portable lock-free queue)

From the repository root (CMake >=3.16 and a C++17 compiler), run:

```bash
cmake -S tools/dmr_esp32_device_probe -B /tmp/dmr-probe-host \
  -DDMR_DEVICE_PROBE_WITH_DECODER=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/dmr-probe-host --parallel
ctest --test-dir /tmp/dmr-probe-host --output-on-failure --verbose \
  > dmr_probe_tests.log 2>&1
```

That tests exactly the production-intended queue template with a **fake
receiver** and reports no real decoder timing. It tests full/no-loss,
all-or-nothing overload, rate validation, source mutation isolation, sequence
holes, deliberate missing IQ, epoch/retune and processor error reset, simulated
10 minutes at 48k, and 30,000 cross-thread producer offers. **No actual RF or
DMR DSP is involved.** To repeat with sanitizers:

```bash
cmake -S tools/dmr_esp32_device_probe -B /tmp/dmr-probe-asan \
  -DDMR_DEVICE_PROBE_WITH_DECODER=OFF -DDMR_DEVICE_PROBE_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/dmr-probe-asan --parallel
ctest --test-dir /tmp/dmr-probe-asan --output-on-failure --verbose \
  > dmr_probe_asan.log 2>&1
```

Both commands save logs to your **current working directory**.

### Native DMR end-to-end synthetic benchmark (separate gate)

On an ordinary cloned OrcSDR repository (all existing `components/` are
present), the default CMake option links actual D0 + D1 modules:

```bash
cmake -S tools/dmr_esp32_device_probe -B /tmp/dmr-probe-real -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/dmr-probe-real --parallel
ctest --test-dir /tmp/dmr-probe-real --output-on-failure --verbose > dmr_probe_native_ctest.log 2>&1
/tmp/dmr-probe-real/dmr_probe_host --seconds 30 --log dmr_probe_host.log
/tmp/dmr-probe-real/dmr_probe_host --seconds 10 --overload --log dmr_probe_host_overload.log
```

`dmr_probe_host` uses deterministic **synthetic** continuous-phase four-level
frequency-modulated IQ with known BS SYNC/Golay slot type. It is **not** a
validated full-LC transmission, real-air recording or guaranteed burst lock;
reported `frontend_locks`, `protocol_events`, `validated_ids` must be read
literally. It processes 240-sample (5ms) blocks using the **actual** integrated
D1 receiver and records throughput, latency, queue drops and `sizeof()`.
The `--overload` mode is intentionally lossy and must **not** pass the normal
48k no-loss acceptance criterion. Run time in the host app is synthetic IQ
seconds, NOT paced wall-clock; `realtime_ratio` is only a desktop diagnostic.
Default logs are saved to the **directory you invoke the binary from**; supply
`--log <absolute/external path>` to choose a different results folder.

Helper scripts `scripts/run_host.sh` / `scripts/run_host.ps1` run the host
Release and sanitizer suite and save two logs in your current directory. The
release build with `WITH_DECODER=ON` remains an independent test unless
executed on a clone with all DMR sources.

## Actual Tab5 / ESP32-P4 instructions (NO AUTOMATIC FLASH)

**Use a spare test Tab5 or a bench device whose current firmware and state you
have backed up.** This standalone image replaces the running application on
the device; it does not attach itself to your installed OrcSDR. Review the
partition layout and device flash contents first. **Do not flash a production
Tab5 or change its C6/SD card without explicit owner approval.** Build only
is safe and does not require a connected device.

1. Install the project's native **ESP-IDF 5.5.4** toolchain and source an ESP-IDF
   environment; verify `idf.py --version`, `IDF_PATH` and ESP32-P4 target.
2. Work from a cloned OrcSDR repo with `feature/dmr-digital-voice-foundation`
   checked out, including the unchanged D0/D1 portable modules.
3. Build the separate *probe* project (not `apps/orcsdr-tab5`):

   ```bash
   idf.py -C tools/dmr_esp32_device_probe/esp_idf set-target esp32p4
   idf.py -C tools/dmr_esp32_device_probe/esp_idf build
   ```

4. In `idf.py menuconfig`, under **DMR device probe (standalone only)**,
   select `30` (or `60`/`120`) seconds and default PSRAM placement.
   Optionally compare internal placement **only if** measured free internal
   memory permits it. A separate `FORCE_OVERLOAD` option deliberately pauses
   the worker 100 ms to prove drop-and-reset handling.
5. **Only with an approved spare device**, save its flash/partition state and
   flash the probe with the selected serial port and an intentionally approved
   partition table. Example (DO NOT execute blindly):

   ```bash
   idf.py -C tools/dmr_esp32_device_probe/esp_idf -p COM17 app-flash
   ```

   `COM17` is an **example** and not a claim about the current device. Default
   probe partition layout may differ from a production Tab5 image. Prefer an
   erased **dedicated test device** to avoid accidentally overwriting NVS,
   recovery data, other partitions or installed firmware.
6. Log **UART / USB Serial-JTAG** from the host terminal without writing to
   microSD. From the directory in which you want logs, run (Linux/macOS):

   ```bash
   idf.py -C /absolute/path/to/OrcSDR/tools/dmr_esp32_device_probe/esp_idf \
     -p /dev/ttyACM0 monitor 2>&1 | tee dmr_probe_tab5_$(date +%Y%m%d_%H%M%S).log
   ```

   Windows PowerShell (adjust the absolute repo path and port):

   ```powershell
   idf.py -C 'C:\OrcSDR\tools\dmr_esp32_device_probe\esp_idf' -p COM17 monitor |
      Tee-Object -FilePath (Join-Path (Get-Location) 'dmr_probe_tab5.log')
   ```

7. Wait for the **`QUALIFICATION_SYNTHETIC`** line before treating any numeric
   report as complete. Preserve complete serial logs, compiler/IDF version,
   target board, actual PSRAM size, run duration, free RAM/stack, p99 and
   max worker latency, missed deadlines, channelized/USB behavior (if later
   explicitly tested), and reset causes. Correlate with existing device
   telemetry under live radio load before any production integration decision.

On-device output includes `STATIC_SIZES`, `HEAP_START`, per-second backlog,
`RESULT`, `WORKER`, `DMR_FRONTEND`, `MEMORY`, and `QUALIFICATION_SYNTHETIC`.
A synthetic success does **not** establish concurrent streaming/USB success.
See [`RESULTS.md`](RESULTS.md) for measured/unmeasured status.

## Acceptance gates

| Gate | Required evidence |
|---|---|
| SPSC safety | Deterministic host tests + sanitizer and cross-thread stress |
| Linked native DMR | Real D0/D1 native build and tests, timing/memory on host |
| Standalone ESP32-P4 | Physical 30–120 s measured 48 kS/s input, no cumulative queue growth, no unexpected drops, no deadline misses, observed heap/stack/PSRAM |
| Concurrent Tab5 SDR | Separate owner-approved firmware integration, same USB owner, 48k channelizer, physical USB underruns and DSP duty under normal dashboards |
| Production readiness | CI, hardware long soak, real-air IQ, tuner/SD/P25 non-interference; all verified separately |

**No main merge, firmware modification, driver change or device flashing was
performed by this workstream.**
