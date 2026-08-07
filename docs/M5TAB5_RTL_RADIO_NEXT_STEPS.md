# M5Tab5 native RTL radio — status and next steps

Date: 2026-08-06

Truth labels used below: **Measured**, **Repo**, **Assumption**, **Unknown**.

This note captures the operator-facing radio path on the Tab5 with the
RTL-SDR.COM V4 on USB-A, the in-tree UI/firmware direction, and the next
evidence gates. It does not replace the clean-room USB contract
(`RTL_SDR_V4_CLEAN_ROOM_SPEC.md`) or the portable driver plan
(`ESP32_RTL_SDR_DRIVER_PLAN.md`).

---

## Hardware and build context

| Item | Value |
|---|---|
| Unit | Measured Tab5 v1.3 / pre-v3 ESP32-P4 |
| Host serial observation | COM17 (observation only; not identity) |
| Dongle | Official RTL-SDR.COM V4 on Tab5 USB-A host |
| UI build env | PlatformIO `m5tab5_ui` in `firmware/m5tab5` |
| Platform pin | pioarduino `55.03.38-1`, Arduino `3.3.8` |
| Libs | M5Unified `0.2.15`, M5GFX `0.2.21` |
| Python for PIO | **3.10–3.13 only** (not 3.14) |
| Toolchain | Platform-supplied ESP-P4 RISC-V; do not override |

```powershell
cd firmware\m5tab5
# Prefer a 3.10–3.13 interpreter, e.g. Python 3.11 on this workstation:
& 'C:\Users\hardc\AppData\Local\Programs\Python\Python311\python.exe' -m platformio run -e m5tab5_ui
& 'C:\Users\hardc\AppData\Local\Programs\Python\Python311\python.exe' -m platformio run -e m5tab5_ui -t upload --upload-port COM17
```

After flash, power-cycle or press reset if the unit remains in download mode.

---

## What is already proven

### **Measured** (physical Tab5 + V4)

- High-Speed USB enumeration of official V4 identity `0bda:2838` serial
  `00000001` from firmware 0.7.2 onward (no PC-side RTL driver).
- Firmware **0.8.23** continuous listen path with fixed presets:
  - **KZEL 96.1 MHz WBFM** — operator confirms audible audio.
  - **NOAA / WX 162.400 MHz NFM** — operator confirms audible audio.
- On-device speaker path, spectrum + waterfall render, authenticated serial
  `RTL_LISTEN` / `RTL_STOP`, and sustained ~960 kS/s class bulk path on 0.8.23
  (see clean-room doc for the numbered firmware ladder and artifact hashes).

### **Repo + flash smoke (2026-08-06)**

Firmware work beyond 0.8.23 lives in `firmware/m5tab5/ui/main.cpp`.

**0.8.25 flash (2026-08-06):** volume default 220, spectrum ~45 FPS, FM tune OK.
Operator report: louder OK, but **touch locked out after waterfall started** and
**audio was choppy** — high-rate draw on the USB task starved Arduino `loop()`
touch and speaker timing.

**0.8.26 response (flashed same day):**

| Change | Why |
|---|---|
| Default volume **128** (~50%) | Operator request |
| Spectrum interval **40 ms** (~25 FPS) | Keep smooth waterfall; free CPU for audio |
| Poll touch **inside** stream loop | Buttons work while SDR is running |
| Skip NVS journal on live VOL taps | Avoid multi-ms flash writes mid-audio |
| Light VOL chip repaint only | Avoid full control-bar redraw cost |
| Prefer audio over spectrum if drops | Degrade graphics before glitching sound |
| `vTaskDelay(0)` between bulks | Yield to other work |

Serial/operator re-check of 0.8.26 is the active Gate A remainder.

### **Repo intent (features under test)**

| Area | Intent |
|---|---|
| Volume | Default 220/255, live `VOL+/-`, mute toggle, `RTL_VOLUME n`, higher DSP headroom |
| Band UI | **FM** / **AM** / **WX**, FREQ±, START/STOP; spectrum + waterfall retained |
| FM tune | Clean-room-validated R820-style PLL packing for broadcast FM |
| AM | Envelope demod + MW UI; HF path not clean-room proven |
| WX | Fixed 162.400 MHz NFM |
| Spectrum FPS | Audio-first cadence; 1 px waterfall + `pushImage`, `RTL_SPECTRUM_FPS` |
| Scope pan | Horizontal flick/drag on spectrum+waterfall retunes FM/AM (M5Unified `wasFlicked` / `wasDragged`) |

---

## Problem framing: choppy spectrum

| Factor | Detail |
|---|---|
| Root cause (0.8.23) | `spectrum_phase < 32` → ~1.8 FPS; full `fillRect` of spectrum each frame |
| Panel capability | Tab5 1280×720 MIPI; community LVGL paths report ~60 FPS class on lighter UIs |
| SDR constraint | Same task does USB bulk + demod + audio + draw; paint steals refill budget |
| Repo response (0.8.25) | Time-based ~50 Hz target + cheaper draw; dual-core split is next structural lever |

---

## Dual-core research (next structural step)

### Facts

- ESP32-P4 on the Tab5: **dual HP cores + LP core** (**Measured** silicon class).
- Current radio path: one FreeRTOS task `rtl_usb_host` (`xTaskCreate`, no core
  pin) owns USB host events **and** `run_rtl_capture` (IQ → DSP → speaker →
  FFT → display). Arduino `loop()` only handles touch/serial/Wi-Fi when not
  inside that capture chain.
- **Assumption:** M5GFX display and speaker APIs should remain single-owner
  (one task). USB host event servicing should not block on heavy FFT/draw.

### Recommended split (design only until measured)

```text
Core 0 — I/O (high priority)
  USB Host Library events
  bulk complete → SPSC ring of fixed bulk slots
  re-submit URB immediately
  never call M5.Display here

Core 1 — compute / UI (medium priority)
  pop ring
  FM/AM demod + M5.Speaker
  FFT + spectrum/waterfall at 20–33 ms
  optional SDR touch handling

loop() — low priority (either core)
  pairing, serial protocol, Wi-Fi inventory, journal
```

### What dual-core will and will not fix

| Helps | Does not magically fix |
|---|---|
| USB refill vs draw contention | Full-frame panel cost |
| Stable effective_sps under UI load | Thread-unsafe concurrent Display calls |
| Headroom for ~40–60 Hz waterfall *and* audio | Shared DRAM/cache thrash if both cores pound large buffers |
| Cleaner metrics (usb_overrun vs dsp_underrun) | LP core for this SDR path |

Pinning the *existing* single sequential task to core 1 alone is expected to
change almost nothing.

### Measurement plan before accepting the split

1. Flash known-good image; baseline on KZEL continuous listen.
2. Record `RTL_SPECTRUM_FPS`, `audio_dropped` / peak / RMS, effective sample
   rate, and subjective audio for ≥60 s.
3. Implement ring + pinned tasks behind a compile flag or clear version bump.
4. Repeat the same procedure; accept only if FPS is smoother **and** drops /
   effective_sps do not regress beyond an agreed bound.
5. Document exact firmware version, interval ms, and counts in this file and
   `PROJECT_STATUS.md`.

---

## Ordered next steps

### Gate A — Flash and measure 0.8.25 radio UI

1. ~~Build/upload `m5tab5_ui` with Python 3.10–3.13~~ **Done 2026-08-06**.
2. ~~Serial: V4 probe, FM tune 96.1, volume=220, FPS≈45, audio_dropped=0~~ **Done**.
3. **Remaining operator checks on glass:** subjective volume (is 220 loud enough?),
   VOL± / mute, FREQ±, WX preset, AM UI (expect experimental RF), STOP/START.
4. Optional: re-open serial while listening for sustained `RTL_SPECTRUM_FPS`.

**Partial pass:** host flash + serial path green. Full Gate A needs operator
UI/audio confirmation.

### Gate B — Spectrum cadence tuning

1. If `audio_dropped` climbs under 20 ms interval, raise
   `kRtlSpectrumIntervalMs` to 25–33 (~30–40 Hz).
2. If FPS is high and CPU headroom remains, trial 16 ms (~60 Hz) and re-measure.
3. Optional: show FPS on the SDR header for field use.

### Gate C — Dual-core USB / DSP split

1. SPSC ring of bulk-sized slots in internal RAM where possible.
2. `xTaskCreatePinnedToCore` for USB (core 0) and DSP+UI (core 1).
3. Counters: `usb_overrun`, `dsp_underrun`, ring depth high-water.
4. Keep Display + Speaker on the DSP/UI task only.
5. Pass Gate A metrics with dual-core enabled.

### Gate D — AM / HF honesty

1. Classic AM MW (520–1710 kHz) needs the V4 HF / direct-sampling path, which
   is **not** in the clean-room 100 MHz init ladder.
2. Until that path is observed and encoded, treat AM MW as UI + envelope demod
   + experimental tune, or constrain “AM” to R828D-reachable ranges only with
   an on-screen label.
3. Optional useful AM that can use proven R828D bands (e.g. airband) is a
   separate product decision—not assumed.

### Gate E — Portable `esp_rtlsdr` extraction

Follow `ESP32_RTL_SDR_DRIVER_PLAN.md`: extract USB/tune/capture from the M5 UI
monolith into a native ESP-IDF component; M5 keeps audio/spectrum/OrcLink UI.
No behavior claim change until Tab5 regression matches current measured radio.

### Gate F — Host / OrcLink capability surface (later)

- Typed capabilities for listen/stop/volume/band (daemon admission still
  separate from device UI).
- Proposal **dispatch** remains unbuilt globally; do not describe on-device
  radio UI as daemon-authorized execution.

### Deferred / out of scope here

- Bias tee, arbitrary gain, PPM calibration, free-form SatDump on-device.
- DMX and laser (operator deferred).
- Production secure boot / encrypted NVS for device credentials.

---

## Serial / UI command cheat sheet (repo)

| Action | UI | Serial (authenticated where required) |
|---|---|---|
| Listen FM (default KZEL-class) | FM / START | `RTL_LISTEN` / `RTL_LISTEN FM` / `RTL_LISTEN KZEL` |
| Listen WX | WX | `RTL_LISTEN NOAA` / `RTL_LISTEN WX` |
| Listen AM | AM | `RTL_LISTEN AM` |
| Stop | STOP | `RTL_STOP` |
| Volume | VOL± / VOL readout | `RTL_VOLUME 0..255` |
| Status | header | `RTL_STATUS`, `RTL_CAPTURE_STATUS` |
| FPS | — | `RTL_SPECTRUM_FPS fps=… audio_dropped=…` (periodic) |

---

## Related documents

| Doc | Role |
|---|---|
| `PROJECT_STATUS.md` | Authoritative measured / unverified / next gate |
| `RTL_SDR_V4_CLEAN_ROOM_SPEC.md` | USB contract and firmware ladder through 0.8.23 |
| `RTL_SDR_INTEGRATION.md` | Windows host adapter + Tab5 native summary |
| `ESP32_RTL_SDR_DRIVER_PLAN.md` | Portable driver extraction plan |
| `firmware/m5tab5/README.md` | Build/flash commands |
| `M5TAB5_VALIDATION_REPORT.md` | Broader Tab5 evidence |
