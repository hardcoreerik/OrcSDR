# OrcSDR project status

Snapshot date: **2026-08-08**
Source branch: **`codex/sdr-bandwidth-navigation` at `20441dc`**
Mainline baseline: **`origin/main` at `bda29fd`**

This is the authoritative current-status and roadmap index. Historical design,
research, and validation documents remain useful evidence, but do not override
this file when their paths, versions, or completion claims differ.

## Evidence labels

| Label | Meaning |
|---|---|
| **Hardware-verified** | Observed on the named physical device and recorded |
| **Build-verified** | Compiled or validated from repository artifacts, without a hardware claim |
| **Implemented** | Present in source; current hardware acceptance may still be pending |
| **Planned** | Accepted roadmap work, not implemented |
| **Deferred** | Intentionally outside the current milestone |

## Current snapshot

| Area | State | Evidence boundary |
|---|---|---|
| RTL-SDRv4-ESP API | **Implemented, v0.4.1** | Header and component source |
| Blog V4 USB identity and 960 kS/s stream | **Hardware-verified** | Tab5 + Blog V4 on the measured USB host path |
| Multi-URB stream, IQ ring, metrics | **Implemented** | Component source; five-minute acceptance still required |
| In-stream hot retune | **Implemented** | Component v0.4.1; settle-time measurement pending |
| Core split | **Implemented** | USB core 0; IQ delivery and app work on core 1 |
| Tab5 radio shell | **Implemented** | FM/AM/WX, radio/scope/capture tabs, sound/GFX toggles |
| SDR navigation | **Implemented on this branch** | Pinch span/filter, navigation drawer, peak find, FM auto tune |
| Variant-4 splash | **Implemented on this branch** | Looping SD asset playback and static ready/button overlay |
| Final splash smoothness | **Pending hardware acceptance** | Confirm the final 24 FPS SD asset with serial `SPLASH_FPS` evidence |
| Graphics with audio enabled | **Open performance gate** | Establish before/after `RTL_SPECTRUM_FPS` and audio-drop evidence |
| AM/HF fidelity | **Experimental** | Do not claim calibrated HF/direct-sampling support |
| Second ESP32-P4 board | **Planned** | No second-board hardware evidence yet |
| rtl_tcp over Ethernet | **Planned** | App does not exist yet |

## Roadmap

### P0 — finish the Tab5 product loop

- [ ] Measure graphics-on/audio-off and graphics-on/audio-on FPS for five minutes.
- [ ] Remove the deliberate audio-on render throttle only after audio drops remain near zero.
- [ ] Optimize measured DSP hot spots before considering handwritten assembly.
- [ ] Confirm sound defaults off, NAV leaves animation live, and controls remain static.
- [ ] Install the final variant-4 SD pack and record stable loop FPS.
- [ ] Run operator acceptance for FM, WX, AM experimental mode, volume, mute,
      start/stop, pinch navigation, peak find, and auto tune.

Exit: smooth scope with audio enabled, no control corruption, approximately zero
audio drops, and a serial log attached to the validation record.

### P1 — close driver reliability and portability

- [ ] Prove 960 kS/s for five minutes at at least 95% effective sample rate with
      zero fatal USB errors.
- [ ] Unplug/replug during streaming and recover to Ready without reboot.
- [ ] Record retune settle time and recovery behavior after a failed retune.
- [ ] Remove the legacy in-app USB path only after a soak build passes.
- [ ] Build and run `examples/p4_serial_smoke` outside the Tab5 UI.
- [ ] Repeat the driver gate on one other ESP32-P4 board.

Exit: portable driver behavior is measured on two P4 boards and the Tab5 app has
no duplicate USB implementation.

### P2 — network transport

- [ ] Add a single-client `rtl_tcp` app over Ethernet.
- [ ] Sustain at least 1.0 MS/s for ten minutes and document drop rate.
- [ ] Add mDNS discovery; evaluate Wi-Fi IQ only after Ethernet is stable.

### Deferred until measurements justify them

- Bias tee, direct sampling/HF, gain/PPM controls, SpyServer, WebSDR, ADS-B,
  multi-client IQ fanout, and ESP32-S2/S3 production-rate claims.
- LP-core DSP offload: the P4 LP core is intended for low-power service work and
  is not the first choice for the 960 kS/s floating-point demodulation path.
- P4 PIE/ESP-DSP assembly kernels: adopt only for a profiler-identified FFT,
  FIR, or vector hot spot with an A/B quality and throughput check.
- P4 PPA: evaluate for framebuffer fill/blend/scale work only; it does not
  replace RF demodulation.

## Commit-derived history

| Date | Commit | Delivered |
|---|---|---|
| 2026-08-06 | `96e0370` | Initial monorepo, Tab5 app, clean-room driver skeleton, specs |
| 2026-08-06 | `fc9678b` | Peer analysis and gap-closing implementation plan |
| 2026-08-06 | `4948c8e` | Hardened standalone public API contract |
| 2026-08-07 | `ba6ffc3` | Gate-2 multi-URB streaming, IQ delivery, component-backed Tab5 radio |
| 2026-08-07 | `bda29fd` | FM quality work and portable radio/scope/capture shell |
| 2026-08-08 | `84cf9a1` | Variant-4 animated boot splash |
| 2026-08-08 | `f972eb3` | Pinch span and filter navigation |
| 2026-08-08 | `f28b131` | FM auto tune and peak controls |
| 2026-08-08 | `0c76bf1` | SDR navigation drawer |
| 2026-08-08 | `269b191` | Muted-mode scope performance prioritization |
| 2026-08-08 | `20441dc` | Scope animation remains live with navigation open |

The last six entries are branch work until merged into `main`; a commit is not
hardware proof and a branch is not considered landed until its merge is verified.

## Verification commands

Use Python 3.11 for the measured PlatformIO toolchain:

```powershell
Set-Location F:\Ai\OrcSDR\apps\orcsdr-tab5
& 'C:\Users\hardc\AppData\Local\Programs\Python\Python311\python.exe' -m platformio run -e m5tab5_ui
& 'C:\Users\hardc\AppData\Local\Programs\Python\Python311\python.exe' -m platformio run -e m5tab5_ui -t upload --upload-port COM17
```

Required runtime lines for the next performance record:

```text
RTL_INSTALL ok v0.4.1 ...
RTL_CORE_SPLIT usb=core0 iq_demod+ui=core1 ...
RTL_SPECTRUM_FPS fps=... audio_dropped=... audio_chunks=...
SPLASH_FPS ...
```

## Documentation map

| Document | Role |
|---|---|
| `PROJECT_STATUS.md` | Current truth, priorities, and evidence boundary |
| `docs/IMPLEMENTATION_FROM_PEER_RESEARCH.md` | Detailed workstreams and design rationale |
| `docs/PORTING.md` | Driver extraction and target gates |
| `docs/API_RTL_SDR_V4_ESP.md` | Public API contract |
| `docs/M5TAB5_VALIDATION_REPORT.md` | Historical hardware evidence |
| `docs/GATE2_IMPLEMENTATION_LOCK.md` | Historical Gate-2 handoff snapshot |
| `docs/OrcSDR_Splash_README.md` | Splash asset and playback contract |
