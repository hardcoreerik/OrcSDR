# OrcSDR

### Portable, standalone software-defined radio on the ESP32-P4

**OrcSDR turns an ESP32-P4 into a self-contained software-defined radio using an RTL-SDR Blog V4 directly over USB — no PC required.**

<p align="center">
  <img src="docs/images/orcsdr-tab5.png"
       alt="OrcSDR running on the M5Stack Tab5"
       width="100%">
</p>

OrcSDR combines a touchscreen radio interface, live spectrum and waterfall visualization, audio, tuning, signal monitoring, and radio tools with a clean-room ESP-IDF USB Host driver for the **RTL-SDR Blog V4**.

The primary reference implementation runs on the **M5Stack Tab5**, powered by the ESP32-P4.

> [!IMPORTANT]
> **Have a M5Stack Tab5 and RTL-SDR Blog V4?**
>
> Jump directly to **[Flash OrcSDR to the M5Stack Tab5](#flash-orcsdr-to-the-m5stack-tab5)** to get started.

> **Project status:** OrcSDR is under active development. The Tab5 application is the reference implementation while the reusable `RTL-SDRv4-ESP` driver continues to be separated and hardened as a standalone ESP-IDF component.

---

## What is OrcSDR?

Traditional RTL-SDR setups usually depend on a desktop computer, Raspberry Pi, or another general-purpose host.

OrcSDR takes a different approach:

```text
┌─────────────────────┐        USB Host        ┌──────────────────┐
│                     │◄──────────────────────►│                  │
│      ESP32-P4       │                        │ RTL-SDR Blog V4  │
│    M5Stack Tab5     │                        │                  │
│                     │                        └────────┬─────────┘
│  DSP                │                                 │
│  Touch UI           │                              RF Input
│  Audio              │
│  Spectrum           │
│  Waterfall          │
│  Radio Controls     │
└─────────────────────┘
```

The ESP32-P4 communicates directly with the RTL-SDR Blog V4 through USB Host, processes radio data locally, and provides the user interface directly on the Tab5 display.

**No desktop SDR application is required.**

---

## OrcSDR Interface

OrcSDR is designed around a touchscreen-first radio experience.

<p align="center">
  <img src="docs/images/orcsdr-tab5_2.png"
       alt="OrcSDR spectrum browser, band navigation, and CB radio interfaces"
       width="100%">
</p>

The interface combines live spectrum and waterfall displays with direct frequency tuning, band navigation, presets, signal monitoring, and dedicated radio modes.

Current interface work includes:

- FM broadcast radio
- AM radio
- Weather Radio / WX
- VHF and amateur-radio spectrum browsing
- CB radio interface
- LoRa monitoring
- Frequency presets
- Spectrum zoom and navigation
- Touch tuning
- Scope scroll-tuning
- Signal strength monitoring
- Speaker and volume control
- IQ capture tools

<p align="center">
  <strong>Ready to try it on real hardware?</strong><br>
  <a href="#flash-orcsdr-to-the-m5stack-tab5">Flash OrcSDR to your M5Stack Tab5 →</a>
</p>

---

# 🚀 Flash OrcSDR to the M5Stack Tab5

> [!IMPORTANT]
> **This is the fastest path to getting OrcSDR running on real hardware.**
>
> The reference application is built for the **M5Stack Tab5 + RTL-SDR Blog V4**.

### What you need

- **M5Stack Tab5**
- **RTL-SDR Blog V4**
- USB cable for flashing the Tab5
- USB connection from the Tab5 USB Host port to the RTL-SDR
- Antenna appropriate for what you want to receive
- Python 3.10–3.13
- PlatformIO

---

### 1. Clone OrcSDR

Open a terminal and clone the repository:

```bash
git clone https://github.com/hardcoreerik/OrcSDR.git
cd OrcSDR/apps/orcsdr-tab5
```

If you already have OrcSDR cloned:

```bash
cd OrcSDR
git pull
cd apps/orcsdr-tab5
```

---

### 2. Connect the M5Stack Tab5

Connect the Tab5 to your computer over USB.

On Windows, determine which COM port was assigned to the device.

It will normally look something like:

```text
COM5
COM8
COM12
```

PlatformIO can also list detected devices:

```bash
pio device list
```

---

### 3. Build OrcSDR

From:

```text
OrcSDR/apps/orcsdr-tab5
```

run:

```bash
pio run -e m5tab5_ui
```

PlatformIO will configure the environment and build the Tab5 firmware.

---

### 4. Flash OrcSDR

Replace `COMxx` with the serial port assigned to your Tab5:

```bash
pio run -e m5tab5_ui -t upload --upload-port COMxx
```

For example:

```bash
pio run -e m5tab5_ui -t upload --upload-port COM8
```

> [!TIP]
> If you don't know the correct port, run:
>
> ```bash
> pio device list
> ```

---

### 5. Connect the RTL-SDR Blog V4

After flashing OrcSDR, connect the SDR to the Tab5's USB Host interface.

```text
              ┌───────────────────┐
              │   M5Stack Tab5    │
              │     ESP32-P4      │
              └─────────┬─────────┘
                        │
                    USB Host
                        │
                        ▼
              ┌───────────────────┐
              │ RTL-SDR Blog V4   │
              └─────────┬─────────┘
                        │
                     RF Input
                        │
                        ▼
                     Antenna
```

---

### 6. Start exploring

Power or reset the Tab5 and OrcSDR should start into the radio interface.

From there you can begin exploring the available radio modes, spectrum display, waterfall, tuning controls, presets, and other SDR tools.

> [!NOTE]
> OrcSDR is under active development. Some modes and controls are experimental and may change between builds.

---

## Features

### Radio

- RTL-SDR Blog V4 connected directly to ESP32-P4 over USB Host
- FM demodulation
- AM demodulation
- Weather Radio / WX
- CB radio interface
- Frequency tuning
- Adjustable bandwidth
- Signal strength monitoring
- Speaker audio
- Volume control
- IQ capture support

### Spectrum & Visualization

- Live spectrum display
- Live waterfall display
- Touch-driven frequency navigation
- Frequency presets
- Spectrum zoom
- Band navigation
- Signal status
- Full-screen M5Stack Tab5 interface

### Radio Tools

- VHF spectrum browsing
- Amateur-radio band navigation
- CB interface
- LoRa monitoring
- IQ capture
- Recording and analysis tooling
- Frequency and bandwidth controls

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
| MCU | **ESP32-P4** |
| Reference device | **M5Stack Tab5** |
| SDR | **RTL-SDR Blog V4** |
| Interface | USB Host |
| USB VID:PID | `0bda:2838` |
| USB manufacturer | `RTLSDRBlog` |
| USB product | `Blog V4` |

The **ESP32-P4 High-Speed USB Host** is the currently measured reference target.

ESP32-S2 and ESP32-S3 support is **not currently claimed** until those platforms have been measured and validated.

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
├── docs/                        Technical documentation
│   ├── API_RTL_SDR_V4_ESP.md
│   ├── PORTING.md
│   ├── FM_DSP_CAPTURE_LAB.md
│   └── RTL_SDR_V4_CLEAN_ROOM_SPEC.md
│
├── tools/                       Capture, transfer, analysis, and test tools
│
├── LICENSE
└── README.md
```

---

# OrcSDR Tab5 Application

`apps/orcsdr-tab5/` contains the primary OrcSDR application.

It provides the user-facing SDR experience on the M5Stack Tab5, including the radio UI, spectrum and waterfall displays, audio controls, touch interaction, and radio-specific modes.

Current UI capabilities include:

- Spectrum visualization
- Waterfall visualization
- FM / AM / WX modes
- CB interface
- LoRa monitoring
- Speaker control
- Volume control
- Touch tuning
- Scope scroll-tuning
- Frequency presets
- Band navigation
- Signal and radio status
- IQ capture tools

Application-specific documentation is available here:

**[`apps/orcsdr-tab5/README.md`](apps/orcsdr-tab5/README.md)**

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

The component is designed to remain independent enough that it can eventually be used in ESP-IDF projects without requiring the OrcSDR Tab5 UI.

---

## Using the Driver Component

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
- USB device identity validation

The driver extraction is still in progress.

Full bulk-stream extraction from the Tab5 application is tracked in:

**[`docs/PORTING.md`](docs/PORTING.md)**

Until **Gate 2** is complete, `start` returns:

```text
RTL_SDR_V4_ESP_ERR_UNSUPPORTED
```

See the current API contract:

**[`docs/API_RTL_SDR_V4_ESP.md`](docs/API_RTL_SDR_V4_ESP.md)**

---

## Device Identity

The driver currently accepts the measured RTL-SDR Blog V4 identity:

```text
VID:          0bda
PID:          2838
Manufacturer: RTLSDRBlog
Product:      Blog V4
```

The initial target is deliberately narrow while the implementation and USB behavior are validated.

---

## Target Support

| MCU | USB Host | Status |
| --- | --- | --- |
| **ESP32-P4** | High-Speed | ✅ Measured reference platform |
| ESP32-S3 | Full-Speed | ⚠️ Not currently claimed |
| ESP32-S2 | Full-Speed | ⚠️ Not currently claimed |

The **M5Stack Tab5 / ESP32-P4** combination is the primary development and validation platform.

---

## Clean-Room Implementation

`RTL-SDRv4-ESP` is being developed as a **clean-room implementation** of the USB behavior required to operate the RTL-SDR Blog V4.

The implementation is not derived from `librtlsdr` source code.

Measured and documented USB behavior lives in:

**[`docs/RTL_SDR_V4_CLEAN_ROOM_SPEC.md`](docs/RTL_SDR_V4_CLEAN_ROOM_SPEC.md)**

The intent is to keep the device behavior specification separate from the ESP32 implementation.

---

## Architecture

```text
                         OrcSDR
                            │
                ┌───────────┴───────────┐
                │                       │
          Tab5 Touch UI            Radio / DSP
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

The long-term goal is to keep the SDR driver modular enough that other ESP32-P4 applications can use it without depending on the OrcSDR user interface.

---

## Development Roadmap

OrcSDR is moving from a working reference radio toward a more reusable embedded SDR platform.

Current areas of development include:

- Complete RTL-SDR V4 bulk stream extraction
- Harden USB transfer handling
- Improve DSP and demodulation paths
- Improve FM audio quality
- Improve spectrum and waterfall performance
- Expand radio modes
- Expand CB, LoRa, and amateur-radio tooling
- Improve frequency and band navigation
- Continue separating radio logic from the Tab5 UI
- Validate additional ESP32 USB Host targets
- Build reusable examples around `RTL-SDRv4-ESP`

Engineering notes and current development documents include:

- [`docs/PORTING.md`](docs/PORTING.md)
- [`docs/M5TAB5_RTL_RADIO_NEXT_STEPS.md`](docs/M5TAB5_RTL_RADIO_NEXT_STEPS.md)
- [`docs/FM_DSP_CAPTURE_LAB.md`](docs/FM_DSP_CAPTURE_LAB.md)
- [`docs/IMPLEMENTATION_FROM_PEER_RESEARCH.md`](docs/IMPLEMENTATION_FROM_PEER_RESEARCH.md)
- [`docs/COMPETITIVE_ANALYSIS_ESP32_RTLSDR.md`](docs/COMPETITIVE_ANALYSIS_ESP32_RTLSDR.md)

---

## Why ESP32-P4?

The ESP32-P4 is an interesting platform for standalone SDR because it combines:

- High-Speed USB
- Significantly more processing capability than earlier ESP32 devices
- Display-oriented peripherals
- Embedded form factor
- ESP-IDF ecosystem
- Enough capability to combine USB radio control, DSP, graphics, audio, and touch interaction in one device

OrcSDR explores how much of the traditional PC-based SDR stack can be moved onto embedded hardware.

The goal isn't to reproduce a desktop SDR workstation on an ESP32.

The goal is to make **useful, portable, self-contained radio appliances** possible using inexpensive SDR hardware and embedded processors.

---

## Contributing

OrcSDR is under active development.

Contributions are welcome in areas including:

- ESP32-P4 USB Host development
- DSP and demodulation
- Spectrum and waterfall rendering
- Radio UI improvements
- Hardware testing
- USB traces and measurements
- RTL-SDR V4 behavior documentation
- Radio-mode development
- Test tooling
- Documentation

When contributing to the RTL-SDR V4 driver, please preserve the project's clean-room development requirements and document the source of any device behavior or measurements used by the implementation.

---

## License

OrcSDR is licensed under **AGPL-3.0-only** by default.

See:

- [`LICENSE`](LICENSE)
- [`LICENSING.md`](LICENSING.md)

for details.

Commercial licensing terms are also available from the maintainer.

---

## Project Links

**Repository**  
https://github.com/hardcoreerik/OrcSDR

**Documentation**  
[`docs/`](docs/)

**M5Stack Tab5 Application**  
[`apps/orcsdr-tab5/`](apps/orcsdr-tab5/)

**RTL-SDRv4-ESP Driver**  
[`components/rtl_sdr_v4_esp/`](components/rtl_sdr_v4_esp/)

---

<p align="center">
  <strong>ESP32-P4 + RTL-SDR Blog V4 + USB Host</strong><br>
  Portable, standalone software-defined radio.
</p>
