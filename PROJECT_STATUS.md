# OrcSDR project status

Snapshot date: **2026-08-09**
Source branch: **`codex/sdr-bandwidth-navigation`**
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
| Tab5 radio shell | **Implemented** | FM/AM/WX/CB/LoRa, radio/scope/capture tabs, sound/GFX toggles |
| CB channel dashboard | **Flashed; operator acceptance pending** | 40-channel AM/USB/LSB plan, 2/3 scope, touch channel dial, clarifier, squelch, live S/RF bar |
| LoRa/Meshtastic receive path | **Flashed; live RF acceptance pending** | 250 ms pre-roll, adaptive 9 dB energy trigger, verified SD bridge, and dashboard message return; synthetic full-chain and COM17 protocol checks pass |
| SDR navigation | **Flashed; operator acceptance pending** | Full 24–1766 MHz browse, US band/use guide, direct entry, pinch, peak find, FM auto tune |
| Browse demodulation | **Partial** | NFM spectrum/listen path; AM/SSB/digital mode selection and sub-24 MHz direct sampling remain open |
| Variant-4 splash | **Implemented on this branch** | Looping SD asset playback and static ready/button overlay |
| Final splash smoothness | **Active performance gate** | Compact 24 FPS pack verified; 25 MHz SPI measured 15–16 FPS |
| Current SD splash asset | **Hardware-verified** | 14,271,890 bytes, 240 frames at 24 FPS; device SHA-256 matches source |
| Splash/USB core isolation | **Hardware-verified** | SD reader pinned to core 1 with a per-frame WDT yield; 28-second run had no WDT/panic |
| In-device SD file transfer | **Hardware-verified** | COM17 list/get/put, staged writes, device SHA-256, rollback |
| Graphics with audio enabled | **Open performance gate** | Establish before/after `RTL_SPECTRUM_FPS` and audio-drop evidence |
| Audio/graphics optimization pass | **Implemented on this branch** | 10 FPS parity target, lighter DSP hot path, timing counters; hardware A/B pending |
| FM post-DSP recording quality | **Hardware-verified** | Ten SD WAVs are valid 48 kHz mono PCM with no clipping or digital-zero gaps; a 12-second capture was clean enough for music fingerprinting |
| Live speaker versus recorded PCM | **Open performance gate** | Recorder taps PCM immediately before `playRaw`; clean WAVs plus poor live sound isolate the remaining fault to speaker queue/DMA/output after the DSP tap |
| Paired FM IQ/WAV DSP lab | **Planned** | Buffer synchronized raw CU8 IQ, post-DSP PCM, and metadata in PSRAM; write after capture and evaluate filter variants offline |
| AM/HF fidelity | **Experimental** | Do not claim calibrated HF/direct-sampling support |
| Second ESP32-P4 board | **Planned** | No second-board hardware evidence yet |
| rtl_tcp over Ethernet | **Planned** | App does not exist yet |

## Roadmap

### P0 — finish the Tab5 product loop

- [ ] Measure graphics-on/audio-off and graphics-on/audio-on FPS for five minutes.
- [x] Remove the deliberate audio-on render throttle; retain a reduced cadence only when drops rise.
- [x] Remove the full-URB app copy, software `double` accumulation, and per-sample `tanhf`.
- [x] Add `dsp_load_pct`, block count, and maximum block time to the FPS log.
- [ ] Verify the optimized path keeps audio drops near zero on hardware.
- [ ] Add the paired FM DSP capture described in `docs/FM_DSP_CAPTURE_LAB.md`:
      5–8 seconds of 960 kS/s CU8 IQ, synchronized 48 kHz PCM, and settings metadata.
- [ ] Save paired captures after reception stops as SigMF data/metadata plus WAV;
      do not write to SD in the real-time receive path.
- [ ] Replay one IQ capture through at least three offline filter variants and
      compare SNR, bandwidth, clipping, discontinuities, and CPU cost.
- [ ] Instrument `playRaw` accepts/rejects and perform an external-microphone
      loopback comparison to separate DMA/queue loss from amplifier/speaker coloration.
- [ ] Confirm sound defaults off, NAV leaves animation live, and controls remain static.
- [ ] Accept BROWSE panning/direct entry and US band-guide labels on the physical display.
- [ ] Accept CB channel snapping, scope taps, dial, AM/USB/LSB voice,
      clarifier, squelch, S/RF display, and six dashboard controls.
- [x] Add adaptive energy-triggered LoRa IQ capture and a host watcher that
      pauses, SHA-256 verifies, decodes, returns text, and resumes the SDR.
- [ ] Transmit a live Meshtastic packet and record `RTL_LORA_ENERGY` through
      `LORA_MESSAGE_OK`, then visually accept the readable Tab5 message.
- [x] Install the final variant-4 SD pack and record stable loop FPS (15–16 FPS at 25 MHz SPI).
- [x] Soak the splash past Ready for 28 seconds with no `task_wdt` reset or panic.
- [x] Copy the 14,271,890-byte compact splash pack through COM17; device SHA-256 matched `AA490D5E…BCD1FA`.
- [ ] Run operator acceptance for FM, WX, AM experimental mode, volume, mute,
      start/stop, pinch navigation, peak find, and auto tune.

Exit: smooth scope with audio enabled, no control corruption, approximately zero
audio drops, a serial log attached to the validation record, and a repeatable
paired IQ/WAV dataset that can drive FM filter decisions without tuning by ear.

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
| 2026-08-08 | `cc463bb` | LoRa dashboard/decoder and CB sideband, clarifier, squelch |

The entries after `bda29fd` are branch work until merged into `main`; a commit is not
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
                    dsp_load_pct=... dsp_blocks=... dsp_block_us_max=...
SPLASH_FPS ...
```

## Documentation map

| Document | Role |
|---|---|
| `PROJECT_STATUS.md` | Current truth, priorities, and evidence boundary |
| `docs/IMPLEMENTATION_FROM_PEER_RESEARCH.md` | Detailed workstreams and design rationale |
| `docs/PORTING.md` | Driver extraction and target gates |
| `docs/API_RTL_SDR_V4_ESP.md` | Public API contract |
| `docs/API_SERIAL_CLI.md` | Tab5 serial CLI — tuning, telemetry, RDS status, presets, file transfer |
| `docs/M5TAB5_VALIDATION_REPORT.md` | Historical hardware evidence |
| `docs/GATE2_IMPLEMENTATION_LOCK.md` | Historical Gate-2 handoff snapshot |
| `docs/OrcSDR_Splash_README.md` | Splash asset and playback contract |
| `docs/cb/README.md` | CB channel, sideband, clarifier, squelch, and asset controls |
| `docs/lora/README.md` | LoRa IQ capture and Meshtastic host-decoder workflow |
| `docs/FM_DSP_CAPTURE_LAB.md` | Measured FM WAV evidence and paired IQ/WAV DSP-lab implementation plan |
