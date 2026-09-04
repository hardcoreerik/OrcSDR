# ESP32 RTL-SDR Driver Plan

> **Historical design, superseded as an execution plan.** The standalone
> `esp_rtl_sdr` driver and Waveshare board operation now exist. The proposed
> paths, API restrictions, and pending milestones below describe the original
> plan, not current capability. Use [PORTING.md](PORTING.md), the
> [current integration contract](API_ESP_RTL_SDR.md), and
> [project status](../PROJECT_STATUS.md) before scheduling work.

## Goal

Extract the physically verified RTL-SDR.COM V4 USB path from the M5Tab5
application into a reusable ESP-IDF component. Prove it first on a second
ESP32-P4 board, then expand only to ESP32 targets whose USB host and sustained
throughput have been measured.

"Universal" means one stable driver API with explicit target capabilities. It
does not mean claiming identical performance on every ESP32. ESP32-P4 has a
USB 2.0 High-Speed host; ESP32-S2 and ESP32-S3 have Full-Speed host support;
other ESP32 families do not have the same native USB-OTG host surface.

## Evidence and implementation boundary

- Preserve the clean-room boundary in `RTL_SDR_V4_CLEAN_ROOM_SPEC.md`.
- Use only the independently observed transfer artifacts, public hardware
  documentation, and public ESP-IDF APIs as driver inputs.
- Keep measured facts, derived behavior, assumptions, and unverified target
  support clearly labeled.
- The M5Tab5 v0.8.23 result is the reference behavior, not the portable
  implementation: 960 kS/s, fixed KZEL and NOAA presets, bounded and
  continuous bulk capture, cleanup, stop, audio DSP, and spectrum rendering.
- Repo firmware 0.8.25 extends the application UI (volume, FM/AM/WX, faster
  spectrum) but is not yet a new measured driver baseline. Operator checklist
  and dual-core USB vs DSP/UI research live in `M5TAB5_RTL_RADIO_NEXT_STEPS.md`.
  Extract the USB/tune/capture core first; keep dual-core ring ownership as a
  driver-internal or app-session concern after the single-task path is stable.
- Do not expose raw USB control transfers, arbitrary tuner registers, bias tee,
  arbitrary gain, or arbitrary frequency through the public API.

## Target architecture

```text
Application / OrcLink / rtl_tcp gateway
                  |
          stable public C API
                  |
       esp_rtlsdr session manager
         |         |          |
    V4 profile  buffer pool  events/metrics
         |
   ESP-IDF USB Host client
         |
 application-owned USB Host Library
         |
       board BSP: VBUS, connector, power policy
```

The reusable component is a USB Host Library **client**. The containing
application normally owns installation and servicing of the ESP-IDF USB Host
Library so this driver can coexist with other USB class drivers. A standalone
example may include a small host-daemon helper, but host-stack ownership must
not be hidden inside the radio driver.

### Component layout

Create a native ESP-IDF component independent of Arduino, M5Unified, displays,
audio codecs, networking, and OrcLink:

```text
firmware/components/esp_rtlsdr/
  CMakeLists.txt
  Kconfig
  idf_component.yml
  include/esp_rtlsdr.h
  src/esp_rtlsdr.c
  src/esp_rtlsdr_usb.c
  src/esp_rtlsdr_v4.c
  src/esp_rtlsdr_profiles.c
  private/rtl_sdr_v4_transfers.h
  test/

firmware/examples/rtl_sdr_p4_smoke/
  main/
  boards/m5stack_tab5/
  boards/waveshare_p4_module_dev_kit/
```

Add thin integrations outside the component:

- M5Tab5 UI/audio/spectrum consumer.
- OrcLink capability adapter.
- Ethernet/WebSocket IQ gateway.
- Optional constrained `rtl_tcp` compatibility gateway.

### Public API shape

Use a C API so ESP-IDF applications can consume it directly and Arduino can
receive a thin wrapper later.

```c
typedef struct esp_rtlsdr_handle *esp_rtlsdr_handle_t;

typedef enum {
    ESP_RTLSDR_PRESET_KZEL_96_1,
    ESP_RTLSDR_PRESET_NOAA_162_4,
} esp_rtlsdr_preset_t;

typedef struct {
    bool host_library_already_installed;
    size_t transfer_bytes;
    size_t transfer_count;
    uint32_t control_timeout_ms;
    esp_rtlsdr_event_cb_t event_cb;
    void *event_context;
} esp_rtlsdr_config_t;

esp_err_t esp_rtlsdr_install(const esp_rtlsdr_config_t *config,
                             esp_rtlsdr_handle_t *out_handle);
esp_err_t esp_rtlsdr_get_device_info(esp_rtlsdr_handle_t handle,
                                     esp_rtlsdr_device_info_t *out_info);
esp_err_t esp_rtlsdr_start(esp_rtlsdr_handle_t handle,
                           const esp_rtlsdr_stream_config_t *config);
esp_err_t esp_rtlsdr_stop(esp_rtlsdr_handle_t handle,
                          uint32_t timeout_ms);
esp_err_t esp_rtlsdr_get_metrics(esp_rtlsdr_handle_t handle,
                                 esp_rtlsdr_metrics_t *out_metrics);
esp_err_t esp_rtlsdr_uninstall(esp_rtlsdr_handle_t handle);
```

The stream configuration initially accepts only an allowlisted preset plus a
bounded byte/duration limit. IQ delivery uses driver-owned buffers delivered
by callback or queue with explicit acquire/release ownership. The callback
must not perform display, audio, network, or blocking work.

### State and ownership

Use one task as the sole owner of the ESP-IDF USB client API and interface 0:

```text
Absent -> Enumerated -> Claimed -> Initializing -> Ready -> Streaming
   ^          |            |            |             |         |
   +----------+------------+------------+-------------+--Stopping
                                                        |
                                                     Failed
```

- Commands enter through a bounded queue.
- USB completion callbacks only record completion and wake the owner task.
- Stop, disconnect, short transfer, unexpected STALL, timeout, and cleanup
  have explicit results.
- One tuner session may have multiple read-only consumers, but only the
  session manager may claim the interface, initialize, retune, or stop it.
- Use at least two DMA-capable internal-RAM bulk buffers. Copy or process into
  PSRAM only after USB completion if the selected target cannot DMA there.
- Track sequence, requested/actual bytes, sample rate, elapsed time, short
  transfers, overruns, consumer drops, minimum, maximum, mean, and SHA-256.

## Phase 1: extraction without behavior change

1. Move transfer execution, expected-STALL handling, V4 identity filtering,
   interface claim/release, initialization, sample-rate setup, fixed presets,
   bulk capture, stop, cleanup, and metrics into `esp_rtlsdr`.
2. Replace M5Tab5 RTL globals with one driver handle and event consumer.
3. Leave M5Tab5 display, touch, speaker DSP, authentication, journal, and
   OrcLink command parsing in the application.
4. Reflash the current Tab5 and reproduce both fixed presets, bounded capture,
   continuous stop, spectrum, and audio with no regression.

Gate: the extracted driver must reproduce the current measured M5Tab5 result
before any Waveshare-specific changes are accepted.

## Phase 2: Waveshare ESP32-P4 bring-up

The likely target is the Waveshare `ESP32-P4-Module-DEV-KIT`, which provides a
jumper-selectable High-Speed USB Type-A host port, 32 MB PSRAM, Ethernet, and
an ESP32-C6 network coprocessor. Confirm the exact product/SKU and schematic
revision before flashing because Waveshare also sells a smaller
`ESP32-P4-Core-DEV-KIT` with an MX1.25 USB header.

### Board preparation

1. Photograph/record the board model, SKU, revision, ESP32-P4 revision, flash,
   PSRAM, power supply, jumper positions, and USB connector used.
2. Inspect the board schematic for VBUS source, current limit, enable polarity,
   and whether the host jumper also routes DP/DM.
3. Use a supply with documented margin for the board and the V4. Do not add
   automatic VBUS cycling until the power circuit has been measured.
4. Start with ESP-IDF and serial logging only: no display, audio, Wi-Fi, or
   OrcLink dependency.

### Evidence ladder

Run each step independently and retain the structured log before proceeding:

1. Boot and report chip revision, heap/PSRAM, IDF version, board identity, and
   selected USB peripheral.
2. Install/service the High-Speed USB Host Library and detect hot-plug/unplug.
3. Enumerate only `0bda:2838`; record configuration, interface, endpoint,
   speed, and serial descriptors.
4. Claim interface 0 and execute the standard control-wrapper probe.
5. Execute all 515 initialization records, accepting only the six documented
   tuner-probe STALLs.
6. Apply the verified 960 kS/s ratio and one fixed preset.
7. Capture exactly 9,600,000 CU8 bytes; report elapsed time, sustained sample
   rate, min/max/mean, SHA-256, short transfers, and drops.
8. Run cleanup, release interface 0, and prove unplug/replug recovery.
9. Repeat bounded capture ten times across warm restart, application reset,
   and cold power-up.
10. Run a 30-minute continuous capture with bounded memory and a controlled
    stop. Require no leaked buffers/tasks and no unreported sample loss.

Gate: both P4 boards pass the same driver-level suite without board conditionals
inside `esp_rtlsdr`. All board differences remain in BSP/example configuration.

## Phase 3: network service proof

Use the Waveshare board's 100 Mbps Ethernet first. The 960 kS/s CU8 stream is
1.92 MB/s before framing, which fits comfortably in nominal Fast Ethernet and
does not fit the current 115,200-baud serial control channel.

1. Add a bounded binary IQ stream with session ID, sequence, observation time,
   preset/frequency, sample rate, format, payload length, and drop counter.
2. Keep control on an authenticated OrcLink capability path. The API grants a
   short-lived stream lease; it does not expose a permanently writable device
   socket.
3. Apply backpressure policy explicitly: bounded queue, drop/disconnect choice,
   counters, and terminal reason. Never let a slow network consumer starve the
   USB owner task.
4. Record only stream metadata and digest in the audit store; raw IQ does not
   pass through SQLite.
5. After the native stream is proven, add a constrained `rtl_tcp` gateway for
   existing applications. Map only verified operations and reject unsupported
   commands rather than silently accepting them.

Gate: a remote client receives a bounded artifact with exactly the expected
byte count and the same SHA-256 produced on-device, followed by a clean stop
and reconnect.

## Phase 4: broader ESP32 support

Maintain a capability matrix rather than a single unsupported promise:

| Tier | Targets | Initial claim |
|---|---|---|
| A | ESP32-P4 High-Speed host boards | Raw CU8 at the verified 960 kS/s profile, subject to per-board power/throughput proof |
| B | ESP32-S2/S3 Full-Speed host boards | Compile and enumerate first; streaming requires a separately derived and physically verified Full-Speed-safe profile |
| C | ESP32 without native USB-OTG host | Unsupported by the native backend; any external host-controller backend is a separate experimental project |

The 960 kS/s CU8 payload alone is 15.36 Mbit/s, exceeding USB Full-Speed's
12 Mbit/s signaling rate before protocol overhead. Therefore S2/S3 cannot use
the current stream profile. A lower valid device sample rate, aggressive
on-device decimation, or derived-product-only mode must be independently
validated before Tier B can claim streaming.

For every new chip/board:

1. Compile against the supported ESP-IDF version.
2. Verify USB host peripheral, PHY, connector routing, VBUS control/current,
   DMA-capable memory, and sustained task budget.
3. Run enumeration, initialization, bounded stream, cleanup, hot-plug, soak,
   and power-cycle gates.
4. Publish an evidence record listing exact hardware, firmware, configuration,
   results, and remaining unknowns.

## Test strategy

- Host-buildable tests: state transitions, request validation, profile lookup,
  expected-STALL classification, timeout/cancel decisions, metric arithmetic,
  buffer ownership, and error mapping.
- Artifact tests: generated compact transfer header matches the checked-in JSON
  source and hashes; no hand-edited divergence.
- Compile matrix: ESP-IDF component builds for P4, S3, and S2 with capability
  guards, even when streaming is not enabled for a target.
- Hardware tests: exact byte bounds, throughput, hash, disconnect at every
  lifecycle stage, repeated start/stop, slow consumer, consumer disconnect,
  device unplug, app restart, and power restart.
- M5Tab5 regression: UI responsiveness, audio queue drops, waterfall/spectrum,
  authenticated stop, journal behavior, and no reintroduction of USB-A rail
  cycling.

## Definition of first portable release

- One native ESP-IDF component, with public headers and no M5-specific include.
- The unchanged API passes on M5Tab5 and the Waveshare P4 board.
- Fixed KZEL and NOAA presets, bounded/continuous capture, stop, cleanup,
  hot-plug recovery, metrics, and buffer ownership are documented and tested.
- A minimal serial example and a bounded Ethernet IQ example are included.
- Unsupported targets and controls fail explicitly.
- Documentation separates build proof, physical proof, and unverified claims.
