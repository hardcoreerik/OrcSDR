# RTL-SDRv4-ESP

Standalone **ESP-IDF component**: USB Host client for the official
**RTL-SDR Blog V4** (`vid=0bda pid=2838`).

| | |
|---|---|
| **Display name** | RTL-SDRv4-ESP |
| **Component id** | `rtl_sdr_v4_esp` |
| **Header** | `rtl_sdr_v4_esp.h` |
| **API version** | **0.4.1** (multi-URB streaming + safe hot retune) |
| **Primary silicon** | ESP32-P4 High-Speed USB host (measured) |
| **Provenance** | Clean-room observed transfers — not a librtlsdr port |

## What this is

- Enumerate and accept only the measured Blog V4 identity
- Clean-room init / cleanup EP0 tables (`private/rtl_sdr_v4_transfers.hpp`)
- Production C API: install → validate → start → IQ events → stop → uninstall
- Designed so **any** ESP-IDF app (not just OrcSDR) can depend on it

## What this is not

- Not a Windows/`rtl_sdr.exe` wrapper (that stays in OrcLink if needed)
- Not SatDump / decoder pipelines
- Not the OrcSDR radio UI (spectrum, speaker, gestures)
- Not a claim of Full-Speed ESP32-S2/S3 support until measured

## Use in an ESP-IDF project

```text
your_project/
  components/
    rtl_sdr_v4_esp/     # this directory (or git submodule / idf_component.yml)
  main/
```

Or from the OrcSDR monorepo, set:

```cmake
set(EXTRA_COMPONENT_DIRS ${CMAKE_SOURCE_DIR}/../../components)
```

```c
#include "rtl_sdr_v4_esp.h"

rtl_sdr_v4_esp_config_t cfg;
rtl_sdr_v4_esp_config_default(&cfg);
cfg.event_cb = my_cb;

if (rtl_sdr_v4_esp_config_validate(&cfg) != ESP_OK) {
    /* bad args */
}

rtl_sdr_v4_esp_handle_t sdr = NULL;
esp_err_t err = rtl_sdr_v4_esp_install(&cfg, &sdr);
if (err != ESP_OK) {
    /* sdr is NULL */
}
```

## Public API quality (v0.4)

| Property | Behavior |
|---|---|
| Validation | `config_validate` / `stream_config_validate`; `struct_size` ABI guard |
| Errors | Component codes + `err_to_name` + `get_last_error` + `state_to_name` |
| State | IDLE / STREAMING / STOPPING / FAULT; fail closed |
| Threading | Per-handle mutex; USB Core0; IQ delivery Core1 |
| Streaming | Multi-URB bulk IN (default **6 × 16 KiB**), clean-room init + 960k + tune |
| Reentrancy | Lifecycle from callback → `ERR_REENTRANT` |
| Idempotence | `stop` / `uninstall(NULL)` safe |
| Feature discovery | `CAP_STREAM`, `CAP_RETUNE`, `CAP_HOTPLUG`, `CAP_METRICS`, `CAP_CUSTOM_HZ` on |
| Contract | See [`docs/API_RTL_SDR_V4_ESP.md`](../../docs/API_RTL_SDR_V4_ESP.md) |

## Extraction status

| API | Status |
|---|---|
| Lifecycle, validation, metrics, errors, reentrancy | **Hardened** |
| Clean-room transfer tables + PLL pack | **In driver** |
| `start` / bulk IQ / ring / EVT_IQ_BLOCK | **Gate 2 implemented (v0.4)** |
| `retune_hz` | **Implemented**; drains bulk before EP0 and resumes the stream |
| Dual-core IQ ring | **Core0 USB / Core1 delivery** |

Measured Tab5 radio behavior (960 kS/s, KZEL/NOAA, continuous listen) remains
the regression reference. See [`PROJECT_STATUS.md`](../../PROJECT_STATUS.md)
for the outstanding soak, hot-plug, and second-board gates.

## Identity filter

Accept only:

```text
vid=0bda pid=2838 manufacturer="RTLSDRBlog" product="Blog V4"
```

## License

AGPL-3.0-only (see repo root `LICENSE`), unless a separate commercial agreement
applies under the Orc dual-license model.
