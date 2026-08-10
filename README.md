# OrcSDR

### Standalone software-defined radio on the ESP32-P4

**OrcSDR turns an ESP32-P4 into a standalone SDR platform using an RTL-SDR Blog V4 directly over USB — no PC required.**

<p align="center">
  <img src="docs/images/orcsdr-tab5.png" alt="OrcSDR running on M5Stack Tab5" width="100%">
</p>

OrcSDR combines a touchscreen radio interface with a clean-room ESP-IDF USB Host driver for the **RTL-SDR Blog V4**.

The reference implementation runs on the **M5Stack Tab5**, providing a self-contained SDR interface with live spectrum visualization, radio controls, audio, tuning, and signal monitoring.

> **Project status:** Active development. The Tab5 application is the reference platform, while the reusable `RTL-SDRv4-ESP` driver is being extracted and hardened as a standalone ESP-IDF component.

---

## What is OrcSDR?

Traditional RTL-SDR setups normally depend on a PC, Raspberry Pi, or similar host.

OrcSDR explores a different approach:

```text
┌─────────────────┐        USB Host        ┌──────────────────┐
│                 │◄──────────────────────►│                  │
│   ESP32-P4      │                        │ RTL-SDR Blog V4  │
│   M5Stack Tab5  │                        │                  │
│                 │                        └────────┬─────────┘
│  DSP + UI       │                                 │
│  Touch Control  │                              RF Input
│  Audio          │
│  Spectrum       │
│  Waterfall      │
└─────────────────┘
```

The ESP32-P4 talks directly to the RTL-SDR V4 over USB Host and handles the radio interface locally.

No desktop SDR application is required.

---

## Features

### Radio

- RTL-SDR Blog V4 connected directly over USB Host
- FM radio
- AM radio
- Weather Radio / WX
- Frequency tuning
- Adjustable bandwidth
- Signal strength monitoring
- Speaker audio
- Volume control
- IQ capture support

### Visualization

- Live spectrum display
- Waterfall display
- Touch-driven tuning
- Signal and radio status
- Full-screen M5Stack Tab5 interface

### Platform

- ESP32-P4
- M5Stack Tab5 reference hardware
- ESP-IDF USB Host
- PlatformIO build environment
- Reusable `RTL-SDRv4-ESP` component
- Clean-room RTL-SDR Blog V4 USB implementation

---

## Hardware

The current reference platform is:

| Component | Hardware |
| --- | --- |
| MCU | ESP32-P4 |
| Reference device | M5Stack Tab5 |
| SDR | RTL-SDR Blog V4 |
| Interface | USB Host |
| USB VID:PID | `0bda:2838` |
| USB manufacturer | `RTLSDRBlog` |
| USB product | `Blog V4` |

The ESP32-P4 High-Speed USB Host is the currently measured reference target.

ESP32-S2 and ESP32-S3 support is **not currently claimed** until those targets have been measured and validated.

---

## Quick Start

### Requirements

- M5Stack Tab5
- RTL-SDR Blog V4
- USB connection between the Tab5 and SDR
- Python 3.10–3.13
- PlatformIO

Clone the repository:

```bash
git clone https://github.com/hardcoreerik/OrcSDR.git
cd OrcSDR
```

Build the Tab5 application:

```bash
cd apps/orcsdr-tab5
pio run -e m5tab5_ui
```

Upload it:

```bash
pio run -e m5tab5_ui -t upload --upload-port COMxx
```

Replace `COMxx` with the serial port for your Tab5.

---

## Repository Layout

```text
OrcSDR/
├── apps/
│   └── orcsdr-tab5/             M5Stack Tab5 SDR application
│
├── components/
│   └── rtl_sdr_v4_esp/          Reusable RTL-SDR Blog V4 driver
│
├── examples/
│   └── p4_serial_smoke/         Minimal ESP32-P4 driver example
│
├── docs/
│   ├── API_RTL_SDR_V4_ESP.md
│   ├── PORTING.md
│   └── RTL_SDR_V4_CLEAN_ROOM_SPEC.md
│
├── LICENSE
└── README.md
```

---

# OrcSDR Tab5

`apps/orcsdr-tab5/` is the reference OrcSDR application.

It provides the user-facing SDR experience on the M5Stack Tab5, including the radio UI, spectrum display, audio controls, and touch interaction.

```text
apps/orcsdr-tab5/
```

Current UI capabilities include:

- Spectrum visualization
- Speaker control
- FM / AM / WX modes
- Volume control
- Touch tuning
- Scope scroll-tuning
- Radio status and signal information

See [`apps/orcsdr-tab5/README.md`](apps/orcsdr-tab5/README.md) for application-specific build and development notes.

---

# RTL-SDRv4-ESP

At the core of OrcSDR is **RTL-SDRv4-ESP**, a standalone ESP-IDF USB Host driver for the RTL-SDR Blog V4.

```text
components/rtl_sdr_v4_esp/
├── include/
│   └── rtl_sdr_v4_esp.h
├── src/
│   └── rtl_sdr_v4_esp.cpp
├── private/
│   └── rtl_sdr_v4_transfers.hpp
├── CMakeLists.txt
├── Kconfig
├── idf_component.yml
└── README.md
```

The component is designed so it can eventually be used independently of the OrcSDR Tab5 application.

### Using the component

Copy or submodule:

```text
components/rtl_sdr_v4_esp
```

into your ESP-IDF project's `components/` directory.

Alternatively:

```cmake
set(EXTRA_COMPONENT_DIRS /path/to/OrcSDR/components)
```

Basic initialization:

```cpp
#include "rtl_sdr_v4_esp.h"

rtl_sdr_v4_esp_config_t cfg;
rtl_sdr_v4_esp_config_default(&cfg);

rtl_sdr_v4_esp_handle_t sdr;

ESP_ERROR_CHECK(
    rtl_sdr_v4_esp_install(&cfg, &sdr)
);
```

---

## Driver Status

The public `RTL-SDRv4-ESP` API is currently **v0.3.0**.

Current API work includes:

- Input validation
- Locking
- Reentrancy protection
- Idempotent teardown
- Capability discovery
- Clean-room transfer tables

The driver extraction is still in progress.

In particular, full bulk-stream extraction from the Tab5 application is tracked in [`docs/PORTING.md`](docs/PORTING.md).

Until **Gate 2** is complete, `start` returns:

```text
RTL_SDR_V4_ESP_ERR_UNSUPPORTED
```

See the API contract for current behavior:

[`docs/API_RTL_SDR_V4_ESP.md`](docs/API_RTL_SDR_V4_ESP.md)

---

## Device Identity

The driver currently accepts the measured RTL-SDR Blog V4 identity:

```text
VID:          0bda
PID:          2838
Manufacturer: RTLSDRBlog
Product:      Blog V4
```

This deliberately keeps the initial hardware target narrow while the implementation is validated.

---

## Target Support

| MCU | USB Host | Status |
| --- | --- | --- |
| **ESP32-P4** | High-Speed | ✅ Measured reference platform |
| ESP32-S3 | Full-Speed | ⚠️ Not currently claimed |
| ESP32-S2 | Full-Speed | ⚠️ Not currently claimed |

The M5Stack Tab5 / ESP32-P4 combination is the primary development and validation platform.

---

## Clean-Room Implementation

RTL-SDRv4-ESP is being developed as a **clean-room implementation** of the USB behavior required to operate the RTL-SDR Blog V4.

The implementation is not derived from `librtlsdr` source code.

Measured and documented USB behavior lives in:

[`docs/RTL_SDR_V4_CLEAN_ROOM_SPEC.md`](docs/RTL_SDR_V4_CLEAN_ROOM_SPEC.md)

This separation is intentional: the goal is to document the device behavior and implement the ESP32 USB Host side from that documented behavior.

---

## Architecture

At a high level:

```text
                 OrcSDR
                    │
        ┌───────────┴───────────┐
        │                       │
   Tab5 Radio UI          Radio / DSP
        │                       │
        └───────────┬───────────┘
                    │
             RTL-SDRv4-ESP
                    │
             ESP-IDF USB Host
                    │
              USB High-Speed
                    │
            RTL-SDR Blog V4
                    │
                  RF
```

The long-term goal is to keep the SDR driver independent enough that other ESP32-P4 applications can use it without requiring the OrcSDR user interface.

---

## Development Roadmap

Current development is focused on moving from a working reference application toward a reusable ESP32 SDR platform.

Major areas include:

- Complete RTL-SDR V4 bulk stream extraction
- Harden USB transfer handling
- Continue separating radio logic from the Tab5 UI
- Improve DSP and demodulation paths
- Improve spectrum and waterfall performance
- Expand radio controls
- Validate additional ESP32 USB Host targets
- Build reusable examples around `RTL-SDRv4-ESP`

See:

- [`docs/PORTING.md`](docs/PORTING.md)
- [`docs/IMPLEMENTATION_FROM_PEER_RESEARCH.md`](docs/IMPLEMENTATION_FROM_PEER_RESEARCH.md)
- [`docs/COMPETITIVE_ANALYSIS_ESP32_RTLSDR.md`](docs/COMPETITIVE_ANALYSIS_ESP32_RTLSDR.md)

for the current engineering notes and roadmap.

---

## Why ESP32-P4?

The ESP32-P4 makes an interesting platform for standalone SDR experiments because it combines:

- High-Speed USB
- Significantly more processing capability than earlier ESP32 devices
- Display-oriented peripherals
- Embedded form factor
- ESP-IDF ecosystem

OrcSDR is exploring how much of the traditional PC-based SDR stack can be moved directly onto that class of embedded hardware.

The goal isn't to turn an ESP32 into a desktop SDR workstation.

The goal is to make **useful, self-contained radio appliances** possible with inexpensive SDR hardware and embedded processors.

---

## Contributing

OrcSDR is under active development.

Bug reports, hardware measurements, USB traces, ESP32-P4 testing, DSP improvements, documentation fixes, and code contributions are welcome.

When contributing to the RTL-SDR V4 driver, please preserve the project's clean-room development requirements and document the source of any device behavior or measurements used by the implementation.

---

## License

OrcSDR is licensed under **AGPL-3.0-only** by default.

See [`LICENSE`](LICENSE) and [`LICENSING.md`](LICENSING.md) for details.

Commercial terms are also available; contact the maintainer for licensing information.

---

## Project Links

**Repository:**  
https://github.com/hardcoreerik/OrcSDR

**Documentation:**  
[`docs/`](docs/)

**Tab5 Application:**  
[`apps/orcsdr-tab5/`](apps/orcsdr-tab5/)

**RTL-SDRv4-ESP Driver:**  
[`components/rtl_sdr_v4_esp/`](components/rtl_sdr_v4_esp/)

---

<p align="center">
  <strong>ESP32-P4 + RTL-SDR Blog V4 + USB Host</strong><br>
  A standalone embedded software-defined radio platform.
</p>
