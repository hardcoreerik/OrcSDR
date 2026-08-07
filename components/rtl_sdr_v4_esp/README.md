# RTL-SDRv4-ESP

Standalone **ESP-IDF component**: USB Host client for the official
**RTL-SDR Blog V4** (`vid=0bda pid=2838`).

| | |
|---|---|
| **Display name** | RTL-SDRv4-ESP |
| **Component id** | `rtl_sdr_v4_esp` |
| **Header** | `rtl_sdr_v4_esp.h` |
| **Primary silicon** | ESP32-P4 High-Speed USB host (measured) |
| **Provenance** | Clean-room observed transfers — not a librtlsdr port |

## What this is

- Enumerate and accept only the measured Blog V4 identity
- Clean-room init / cleanup EP0 tables (`private/rtl_sdr_v4_transfers.hpp`)
- Public C API for install → start → IQ events → stop
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

rtl_sdr_v4_esp_handle_t sdr;
ESP_ERROR_CHECK(rtl_sdr_v4_esp_install(&cfg, &sdr));
```

## Extraction status

| API | Status |
|---|---|
| `install` / `uninstall` / defaults | **Implemented** (skeleton) |
| Clean-room transfer tables linked | **Present** |
| `start` / bulk IQ / `retune_hz` | **Not finished** — logic still in `apps/orcsdr-tab5` |
| Dual-core IQ ring | Planned (see `docs/PORTING.md`) |

Measured Tab5 radio behavior (960 kS/s, KZEL/NOAA, continuous listen) is the
reference implementation under `apps/orcsdr-tab5` until extraction completes.

## Identity filter

Accept only:

```text
vid=0bda pid=2838 manufacturer="RTLSDRBlog" product="Blog V4"
```

## License

AGPL-3.0-only (see repo root `LICENSE`), unless a separate commercial agreement
applies under the Orc dual-license model.
