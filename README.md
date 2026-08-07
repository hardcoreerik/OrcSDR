# OrcSDR

**OrcSDR** is an ESP32 software-defined radio project.

It uses the standalone **RTL-SDRv4-ESP** driver — a clean-room ESP-IDF USB Host
client for the official **RTL-SDR Blog V4** (`0bda:2838`).

```text
OrcSDR (app / examples / docs)
    └── RTL-SDRv4-ESP  components/rtl_sdr_v4_esp
            └── clean-room V4 USB host driver
```

OrcLink remains the control-plane / multi-adapter system. OrcSDR owns the
**ESP-attached V4 radio** path and the reusable driver.

## Repository layout

| Path | Purpose |
|---|---|
| [`components/rtl_sdr_v4_esp/`](components/rtl_sdr_v4_esp/) | **Standalone driver** (drop into any ESP-IDF project) |
| [`apps/orcsdr-tab5/`](apps/orcsdr-tab5/) | Measured M5Stack Tab5 radio UI (reference app) |
| [`examples/p4_serial_smoke/`](examples/p4_serial_smoke/) | Minimal IDF app that links the driver |
| [`docs/`](docs/) | Clean-room spec, porting matrix, radio notes |

## RTL-SDRv4-ESP (standalone driver)

```text
components/rtl_sdr_v4_esp/
  include/rtl_sdr_v4_esp.h     Public C API
  src/rtl_sdr_v4_esp.cpp       Lifecycle + extraction hooks
  private/rtl_sdr_v4_transfers.hpp
  CMakeLists.txt
  Kconfig
  idf_component.yml
  README.md
```

### Use as a standalone component

Copy or submodule `components/rtl_sdr_v4_esp` into your project’s `components/`
directory, or set:

```cmake
set(EXTRA_COMPONENT_DIRS /path/to/OrcSDR/components)
```

```c
#include "rtl_sdr_v4_esp.h"

rtl_sdr_v4_esp_config_t cfg;
rtl_sdr_v4_esp_config_default(&cfg);
rtl_sdr_v4_esp_handle_t sdr;
ESP_ERROR_CHECK(rtl_sdr_v4_esp_install(&cfg, &sdr));
```

**Status:** install/uninstall and transfer tables are in-tree. Full bulk stream
extraction from the Tab5 app is tracked in [`docs/PORTING.md`](docs/PORTING.md)
(`start` currently returns `ESP_ERR_NOT_FINISHED`).

### Identity filter

Only the measured Blog V4 is accepted:

```text
vid=0bda pid=2838  manufacturer="RTLSDRBlog"  product="Blog V4"
```

### Target matrix

| MCU | USB host | Claim |
|---|---|---|
| ESP32-P4 | High-Speed | Measured reference (Tab5) |
| ESP32-S3 / S2 | Full-Speed | Not claimed until measured |

## OrcSDR Tab5 app (reference)

PlatformIO / M5Unified radio UI with spectrum, speaker, FM/AM/WX, volume, and
scope scroll-tune. Build notes: [`apps/orcsdr-tab5/README.md`](apps/orcsdr-tab5/README.md).

```powershell
cd apps/orcsdr-tab5
# Python 3.10–3.13 required for pioarduino 55.03.38-1
pio run -e m5tab5_ui
pio run -e m5tab5_ui -t upload --upload-port COMxx
```

## Provenance

USB behavior is documented in
[`docs/RTL_SDR_V4_CLEAN_ROOM_SPEC.md`](docs/RTL_SDR_V4_CLEAN_ROOM_SPEC.md).
This is **not** derived from librtlsdr source.

## Relationship to OrcLink

| OrcLink | OrcSDR |
|---|---|
| Daemon, policy, Windows adapters, Control Room | ESP V4 host driver + radio apps |
| May later *consume* OrcSDR/device evidence | Does not embed orclinkd |

## License

AGPL-3.0-only by default (see [LICENSE](LICENSE)). Commercial terms available
under the same dual-license model as other Orc projects — contact the maintainer.

## Peer research and roadmap

| Doc | Purpose |
|---|---|
| [COMPETITIVE_ANALYSIS_ESP32_RTLSDR.md](docs/COMPETITIVE_ANALYSIS_ESP32_RTLSDR.md) | ADS-B Scope, esp32p4-wifi-rtlsdr, xtrsdr vs OrcSDR |
| [IMPLEMENTATION_FROM_PEER_RESEARCH.md](docs/IMPLEMENTATION_FROM_PEER_RESEARCH.md) | Gap-closing implementation plan (P0–P3) |
| [PORTING.md](docs/PORTING.md) | Driver extraction and board matrix |

## Links

- GitHub: https://github.com/hardcoreerik/OrcSDR  
- Sibling: https://github.com/hardcoreerik/OrcLink  
