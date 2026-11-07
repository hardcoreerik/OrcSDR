# OrcSDR Cardputer Zero / Linux bring-up (experimental)

This independent target is the first Linux receiver experiment for OrcSDR, **not** a port of the ESP32-P4 firmware. It does not require ESP-IDF or M5GFX. The initial executable is a headless, receive-only RTL-SDR diagnostic with synthetic IQ tests and unsigned 8-bit IQ-file analysis. **No Cardputer Zero GUI, audio or physical-device validation exists yet.**

## Native Linux build / test

Requires CMake 3.16+, a C++17 compiler, and optionally pkg-config and the platform's librtlsdr-dev package. Without librtlsdr the offline program builds, but receiver list/capture commands fail clearly.

    cmake -S apps/orcsdr-cardputer-zero -B build/cardputer-zero-native -DCMAKE_BUILD_TYPE=Release
    cmake --build build/cardputer-zero-native --parallel
    ctest --test-dir build/cardputer-zero-native --output-on-failure
    ./build/cardputer-zero-native/orcsdr_cardputer_zero --demo
    ./build/cardputer-zero-native/orcsdr_cardputer_zero --help

Run from the repository root. Save diagnostics to a log in the working directory:

    ./build/cardputer-zero-native/orcsdr_cardputer_zero --demo 2>&1 | tee cardputer-zero-bringup.log

Analyze a saved unsigned 8-bit interleaved I/Q file (I0,Q0,I1,Q1,...), not a WAV, float32 or signed IQ file:

    ./build/cardputer-zero-native/orcsdr_cardputer_zero --iq-file /path/to/capture.u8 --samples 32768

## First physical device smoke test (NOT performed)

1. Install the official Cardputer Zero Linux OS using the manufacturer's imager. Back up existing microSD first. Record outputs of uname -m, cat /etc/os-release, free -h and lsusb.
2. Set the Cardputer Zero USB selector to **Host**. In Slave mode the hub, USB-A and Ethernet are disconnected. Connect the RTL-SDR to USB-A; use a powered hub if power is uncertain. Check physical USB enumeration and udev permissions.
3. If available on the target OS install cmake, g++, pkg-config and librtlsdr-dev. Build natively as above. Confirm CMake says "system librtlsdr enabled".
4. Run the receiver enumeration command and confirm the tuner:

       ./build/cardputer-zero-native/orcsdr_cardputer_zero --list

5. With a suitable antenna and an allowed test signal, take a bounded receiver sample, comparing against a known-good Linux host:

       ./build/cardputer-zero-native/orcsdr_cardputer_zero --capture --freq 100100000 --rate 1024000 --samples 32768

6. Record dongle type, USB power, sample rate, frequency, CPU and memory usage and receiver errors. Continuous real-time streaming, drop-accounting, audio, keyboard and display remain separate acceptance gates.

The 64-bin DFT and digital mean power are rough uncalibrated diagnostics, **not dBm or production spectrum**. Physical frequency coverage depends on the tuner and Linux driver, not CLI numeric limits. This target uses Linux **librtlsdr**, not the ESP-IDF esp-rtl-sdr host transport.

## Official SDK and display plan

Use [CardputerZero/Template](https://github.com/CardputerZero/Template) for LVGL, CMake, SDL desktop preview, Linux input/display and ARM64 build guidance, and [CardputerZero/AppBuilder](https://github.com/CardputerZero/AppBuilder) for packaging. Its cross-toolchain references an upstream CM0 BSP sysroot release (v0.0.4 in the checked template). Before importing any template code, pin the exact upstream commit, preserve licensing, build desktop simulation and test an ARM64 Debian package.

Proposed 320x170 screen: tuned frequency, receiver connection status, one S-meter row and a focused spectrum/waterfall, with keyboard actions for tune, gain, band, save and exit. No attempt to squeeze the Tab5 1280x720 touchscreen UI directly onto this device.

Details: [research and bring-up](../../../docs/hardware/cardputer-zero/RESEARCH_AND_BRINGUP.md) and [portability architecture](../../../docs/hardware/cardputer-zero/PORTABILITY_ARCHITECTURE.md).

## Native OrcSDR decoder integration (2026-11-06)

The Cardputer Linux target now compiles **the original OrcSDR source files** `acars_core.cpp`, `acars_selftest.cpp` and `adsb_decoder.cpp` from `apps/orcsdr-tab5/ui` directly. No copied decoder fork and no ESP-IDF firmware modifications. The host CTest suite covers ten ACARS simulated RF blocks, CRC/repairs, chunked replay, stream reset, ADS-B known frames/CRC and bounded CU8 queue/drop accounting. Actual Cardputer hardware is **not yet tested**.

Additional CLI modes:

```sh
./build/cardputer-zero-native/orcsdr_cardputer_zero --acars-demo
./build/cardputer-zero-native/orcsdr_cardputer_zero --decode-acars /path/to/acars-240k.cu8 --freq 131490000
./build/cardputer-zero-native/orcsdr_cardputer_zero --decode-adsb /path/to/adsb-2048k.cu8
```

The ACARS replay file must contain CU8 at 240 kS/s, wanted carrier at +60 kHz (example actual channel 131.550 MHz). ADS-B replay requires CU8 at 2.048 MS/s tuned near 1090 MHz. These are genuine decoders, not synthetic text injectors, but still offline file modes; live streaming into a decoder and Web server remain future gates.

See [decoder/Linux acceptance and reproducible log commands](../../../docs/hardware/cardputer-zero/ORCDEC_LINUX_ACCEPTANCE_20261106.md) and the [actual OrcSDR Web interoperability plan](../../../docs/hardware/cardputer-zero/LINUX_WEB_INTEROPERABILITY_PLAN.md).
