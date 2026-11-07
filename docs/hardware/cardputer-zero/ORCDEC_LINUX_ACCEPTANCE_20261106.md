# Cardputer Zero / OrcSDR native decoder and Linux acceptance

Status: host-integrated, CI under test; **not physically tested on Cardputer Zero**. Base: `196c5405db6a907a6d995ba103e811763cb95878`. Development branch: `agents/cardputer-zero-orcdec-20261106`.

## What is already implemented

- The Cardputer Linux CMake target links **unchanged source files** from `apps/orcsdr-tab5/ui/acars_core.cpp`, `acars_selftest.cpp` and `adsb_decoder.cpp`. There is no extra ACARS/ADS-B fork, no new decoder algorithm and no ESP32 firmware change.
- The existing 240 kS/s ACARS signal generator runs the original ACARS FIR/channel and frame decoder for 10 fixed cases with distinct bit phase and noise. CTest additionally tests CRC zero residue, verified one/two-bit repairs, rejection of a three-bit error, stream reset retaining stats, and identical message delivery over four varying CU8 chunk sizes.
- The original ADS-B `Decoder::self_check()` exercises existing aircraft frames, CRC, CPR positions and demodulation from deterministic magnitude fixtures.
- `--acars-demo` and `--decode-acars PATH` call that exact ACARS implementation in the Linux application. `--decode-adsb PATH` calls the exact ADS-B decoder. An empty file or a file with no verified messages is not reported as a successful decode.
- The Linux receiver now has a move-proof RAII owner, separate enumeration, open/configure/read/close stages, driver readback for tuning and sample rate, gain list/tuner ID capability reporting, and named failures. Only system `librtlsdr` is used for actual Linux USB access.
- `src/iq_queue.hpp` is a preallocated, bounded and thread-safe CU8 handoff. It accounts for known samples discarded because the queue was full or flushed, and marks a discontinuity on the next accepted block. USB loss not reported by the driver remains **unknown**, not zero. This queue is a **foundation**; a continuous receiver/decoder worker has not yet been wired to it.

**Not done:** live ACARS/ADS-B receiver loop, sustained SDR sample-loss measurements, sound output, LCD input/display, Linux Web HTTP service, Cardputer ARM64 target build, device installation or RF tests. Decoder fixtures are not evidence of live radio reception.

## Exact host build/test commands

Run from the **repository root**. All logs below are saved in the **same working directory where commands are executed**.

```bash
cmake -S apps/orcsdr-cardputer-zero -B build/cardputer-zero-offline \
  -DCMAKE_BUILD_TYPE=Release -DORC_ENABLE_RTLSDR=OFF 2>&1 | tee cardputer-zero-config-offline.log
cmake --build build/cardputer-zero-offline --parallel 2>&1 | tee cardputer-zero-build-offline.log
ctest --test-dir build/cardputer-zero-offline --output-on-failure --verbose 2>&1 | tee cardputer-zero-test-offline.log
./build/cardputer-zero-offline/orcsdr_cardputer_zero --demo 2>&1 | tee cardputer-zero-demo.log
./build/cardputer-zero-offline/orcsdr_cardputer_zero --acars-demo 2>&1 | tee cardputer-zero-acars-demo.log

cmake -S apps/orcsdr-cardputer-zero -B build/cardputer-zero-rtl \
  -DCMAKE_BUILD_TYPE=Release -DORC_ENABLE_RTLSDR=ON 2>&1 | tee cardputer-zero-config-rtl.log
cmake --build build/cardputer-zero-rtl --parallel 2>&1 | tee cardputer-zero-build-rtl.log
ctest --test-dir build/cardputer-zero-rtl --output-on-failure --verbose 2>&1 | tee cardputer-zero-test-rtl.log
./build/cardputer-zero-rtl/orcsdr_cardputer_zero --list 2>&1 | tee cardputer-zero-list.log
```

The `ON` option auto-detects `librtlsdr`; inspect CMake's `system librtlsdr enabled` message. If absent, that ON build is effectively offline. The CI matrix installs `librtlsdr-dev` explicitly for the ON case.

Input format: **interleaved unsigned CU8**, not S16, WAV, complex floats or a generic FFT buffer. The ACARS file must be sampled at **240,000 S/s** with the wanted 131.550 MHz carrier at **+60 kHz** relative to its SDR tuning centre; thus `--freq 131490000` is an example. ADS-B must be sampled at **2,048,000 S/s** centered near **1090 MHz**. The CLI does not resample or shift these formats.

```bash
./build/cardputer-zero-rtl/orcsdr_cardputer_zero --decode-acars acars-240k.cu8 --freq 131490000 2>&1 | tee cardputer-zero-acars-replay.log
./build/cardputer-zero-rtl/orcsdr_cardputer_zero --decode-adsb adsb-2048k.cu8 2>&1 | tee cardputer-zero-adsb-replay.log
```

## Continuous IQ contract (next engineering stage)

Use a dedicated receiver producer and decoder consumer. Start with 8 fixed blocks of 32,768 complex CU8 samples each (512 KiB of IQ in flight); adjust only from measured CPU and RAM. Every block carries receiver generation, requested/actual sample rate, tuned center, monotonic capture timestamp, samples explicitly lost before this block and a discontinuity flag. The producer must never block inside decoder or Web callbacks; if no slot is free, discard incoming samples, increment the known drop counter by the **exact number of complex samples discarded** and flag a stream discontinuity. On retune, stop, restart or receiver removal, discard queued old-generation data and reset the ACARS front end + frame decoder (or ADS-B decoder) before newer-generation blocks enter. Keep the decoder's cumulative health counters separate from resettable signal state.

For RTL-SDR, `rtlsdr_read_sync()` in a dedicated worker is adequate for an initial bounded prototype; `rtlsdr_read_async()` should be evaluated once the driver stop/cancel flow is proven. Neither API guarantees that upstream kernel/USB sample gaps can be counted unless the transport reports them. Do not report unknown driver-side losses as `dropped_samples=0` meaning lossless. Make bounded flow and discontinuity semantics compatible with the separately versioned Radio Service contract; do not invent a parallel public protocol.

Acceptance: artificially stall consumer and verify exact queue drops; retune while busy and prove no old-generation IQ enters a new decoder; disconnect receiver while capturing and prove deterministic shutdown; run 30-minute bounded captures with real USB host and record throughput, capture counts, application drops, unknown USB loss indicators, CPU, RSS, heat and power.

## Cardputer Zero physical hardware checklist — all pending

Official specification: M5Stack CardputerZero CM0 (A53 ARM64, 512 MiB), 320 x 170 LCD, keyboard controller, USB 2.0 through a hub, shared 10/100 Ethernet. Official documents:
- https://docs.m5stack.com/en/CardputerZero
- https://docs.m5stack.com/en/compute/cardputerzero/usage

- [ ] Record unit revision/SKU, Debian/Linux OS image identifier + SHA256, kernel and architecture (`uname -a; uname -m; cat /etc/os-release`), USB and SDR power arrangements. Do not overwrite the unit's OS without imaging its existing microSD.
- [ ] Place the device's USB switch in **Host**. Its Ethernet and USB-A hub are disconnected in Slave mode. Check `lsusb; dmesg -w`. Use powered USB hub if necessary.
- [ ] Install compatible system `librtlsdr` + development headers, confirm `pkg-config --modversion librtlsdr`, enumerate tuner and serial with `--list`, and verify non-root device permissions.
- [ ] Test `--capture --device 0 --freq 100100000 --rate 1024000 --samples 32768`; compare actual tuned frequency/sample-rate readbacks and captured sample count with a known-good Linux host. Confirm each reported error path for absent device, unplug and invalid tuning.
- [ ] Record which sample rates the **actual dongle/driver** accepts; 240 kS/s ACARS requires a tested suitable receiver and exact input conditioning. Some Linux drivers may reject rates at the nominal lower boundary.
- [ ] Test ACARS/ADS-B replay and sustained live signal decoding on the ARM64 device with validated antenna, location and on-air signal. Compare to original OrcSDR and an independent reference, including reset/discontinuity behavior.
- [ ] Select and verify installed display backend (fbdev or DRM), evdev keyboard mapping and SDL preview; never assume vendor key shortcuts map to the same Linux key codes after OS changes.
- [ ] Measure under powered USB-A and battery operation: 512 MiB memory pressure, USB hub sharing with Ethernet, CPU thermal throttling, current draw and battery endurance.
- [ ] Prove non-root package installation, service lifecycle, logs, graceful shutdown and Web access security before exposing the device to LAN.
- [ ] Attach exact firmware/Linux image/driver revision, dates, commands, log files and RF evidence to the acceptance report.

## Official Linux SDK/BSP inputs

- CardputerZero/Template: https://github.com/CardputerZero/Template — LVGL 9 application skeleton, SDL desktop preview, CMake, evdev, fbdev/DRM backend choices; inspected upstream revision `477fa6d217b683e3084c361a757f03dc8ca9f753` (2026-10-12). **Do not vendor blindly.**
- CardputerZero/M5CardputerZero-UserDemo: https://github.com/CardputerZero/M5CardputerZero-UserDemo — board demo and BSP/sysroot reference.
- m5stack/M5Stack_Linux_Libs: https://github.com/m5stack/M5Stack_Linux_Libs — Linux-oriented board integration.
- CardputerZero/AppBuilder: https://github.com/CardputerZero/AppBuilder — package and store flow; inspected revision `18996b694ae305306fc5d457d982cfba0b0e7cc5` (2026-10-26).

Pin verified SDK revision, toolchain/sysroot checksum, ABI, kernel and license. Build LVGL application separately from radio service. The LCD is an optional compact controller, not a 320 x 170 reimplementation of OrcSDR Web.
