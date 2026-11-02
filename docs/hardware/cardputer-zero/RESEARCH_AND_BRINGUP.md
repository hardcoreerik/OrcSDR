# Cardputer Zero: research and evidence ledger

Date: 2026-11-02. Target: M5Stack CardputerZero (Linux-based Raspberry Pi CM0).
Branch: codex/cardputer-zero-linux-bringup-20261102. **Not merged into main; hardware not available for testing.**

## Public manufacturer evidence

| Area | Primary source | Finding | Remaining validation |
| --- | --- | --- | --- |
| Hardware | https://docs.m5stack.com/en/CardputerZero | Quad-core Cortex-A53 1GHz, 512MB, 320x170 LCD, 46-key keyboard | Confirm board and OS versions |
| USB host | https://docs.m5stack.com/en/compute/cardputerzero/usage | Host switch routes USB through internal hub to USB-A and other ports; Slave disconnects the hub | SDR enumeration, bus power, throughput |
| UI SDK | https://github.com/CardputerZero/Template | CMake/LVGL, desktop SDL simulator, evdev, Linux framebuffer/DRM | Pin revision, compile and run |
| Cross-toolchain | https://github.com/CardputerZero/Template/blob/main/cmake/toolchains/cp0-aarch64-linux-gnu.cmake | CM0 BSP sysroot download from UserDemo release, v0.0.4 in inspected code | Archive hash, license and ABI |
| Vendor BSP | https://github.com/CardputerZero/M5CardputerZero-UserDemo | Official SDK/BSP reference | Inspect full SDK sources |
| Device libraries | https://github.com/m5stack/M5Stack_Linux_Libs | Linux board-support reference | Verify installed OS compatibility |
| App packaging | https://github.com/CardputerZero/AppBuilder | ARM64 Debian package and AppStore workflow | Create compliant non-root package |
| Community examples | https://github.com/CardputerZero/ | Emulator, launcher and example apps | Verify key mappings and UI lifecycle |

The top-level product page still labels SDK and kernel links "coming soon", but public development repositories already exist. Availability of code is not evidence of compatibility with the delivered unit.

## Engineering evidence (2026-11-02)

- [x] Independent branch from main; Tab5 production firmware unchanged.
- [x] C++17 diagnostic application, synthetic IQ generation and file-based unsigned u8 IQ analysis.
- [x] Optional Linux librtlsdr device enumeration, tuning and bounded synchronous capture path implemented.
- [x] Sandbox x86_64 compilation and two CTest cases passed with synthetic data.
- [ ] Cross-compile with official CM0 BSP (sandbox could not download dependencies).
- [ ] Link against target librtlsdr and enumerate actual receiver hardware.
- [ ] USB-powered dongle, sustained IQ streaming/backpressure and sample integrity.
- [ ] LVGL 320x170 desktop and physical display, keyboard, audio, settings and safe exit.
- [ ] ARM64 installable non-root Debian package.
- [ ] Battery and thermal measurements, field validation and user documentation.

## Risks and reproducibility

- USB 2.0 host is behind a hub: do not infer throughput or reliability from ESP32-P4's direct high-speed USB results.
- Receiver power draw may exceed a comfortable battery/USB budget. Try a powered hub first.
- The ESP-IDF esp-rtl-sdr driver is not directly reusable on Linux; use an independent host transport, without duplicating DSP semantics.
- M5GFX is not the Linux UI system. LVGL needs a separate renderer and evdev keyboard handling.
- Verify SDK provenance/checksums and each redistributed library's license before vendoring.
- The current app is receive-only. It makes no TX claims.

## Capture fields when hardware arrives

Record SKU/revision, OS image source and SHA256, uname/kernel, lsusb and dmesg, SDR model and VID:PID, power source, tested device/sample rates, sustained IQ drops, CPU/memory, battery draw, display/keyboard behavior and a timestamped log in the run directory. Use OrcSDR evidence language: Build-Verified, Runtime-Verified, Hardware-Verified and RF-Verified are different states.
