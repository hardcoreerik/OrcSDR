# M5Tab5 RTL radio next steps

Updated: **2026-08-08**
Authority: [`../PROJECT_STATUS.md`](../PROJECT_STATUS.md)

This document is the Tab5 execution checklist. It intentionally contains no
future feature inventory; the project roadmap owns that scope.

## Current measured target

| Item | Value |
|---|---|
| Board | M5Stack Tab5, ESP32-P4 revision v1.3 |
| Radio | RTL-SDR Blog V4, `0bda:2838`, High-Speed USB |
| Serial | COM17 for the current bench unit; not a device identity |
| App | `apps/orcsdr-tab5` |
| Driver | RTL-SDRv4-ESP v0.4.1 |
| Stream | 960 kS/s CU8 |
| Build | Python 3.11 + PlatformIO environment `m5tab5_ui` |

## Next acceptance run

1. Build and flash the current branch.
2. Start serial capture before reset.
3. Run graphics on with sound off for five minutes.
4. Run graphics on with sound on for five minutes at the same station and span.
5. Exercise NAV, pinch span/filter, peak find, auto tune, volume, mute, FM, WX,
   AM experimental mode, STOP, and START.
6. Confirm controls remain static and scope/waterfall animation never stops when
   NAV is open.
7. Record `RTL_SPECTRUM_FPS`, `audio_dropped`, `audio_chunks`, USB errors,
   effective sample rate, and operator notes.

Pass criteria:

- At least 95% effective sample rate for five minutes.
- Zero fatal USB errors.
- Audio drop count approximately zero and not continuously increasing.
- No visible control redraw, tearing, or frozen animation.
- Audio-on scope cadence is materially comparable to muted cadence.

## Performance order

1. Remove avoidable work in the existing C++ hot path (`double`, transcendental
   math, copies) and compare serial FPS/drop counters.
2. Keep USB ownership on HP core 0 and UI on HP core 1; adjust task work only
   with measured before/after evidence.
3. Use Espressif ESP-DSP/PIE optimized kernels for a measured FFT/FIR/vector hot
   spot before writing custom assembly.
4. Use the P4 PPA only for supported pixel fill/blend/scale operations.
5. Do not move the 960 kS/s floating-point demodulator to the LP core unless a
   prototype proves throughput and memory-transfer benefit.

## Splash gate

Install the final variant-4 `.orsplash` pack on microSD, then capture serial
`SPLASH_FPS` output across multiple seamless loops. The last known firmware
path is implemented; final asset smoothness remains a device/SD-card check.

## Build and flash

```powershell
Set-Location F:\Ai\OrcSDR\apps\orcsdr-tab5
& 'C:\Users\hardc\AppData\Local\Programs\Python\Python311\python.exe' -m platformio run -e m5tab5_ui
& 'C:\Users\hardc\AppData\Local\Programs\Python\Python311\python.exe' -m platformio run -e m5tab5_ui -t upload --upload-port COM17
```

Do not claim completion from a successful build or flash alone. Attach the
serial evidence and operator result to the validation record.
