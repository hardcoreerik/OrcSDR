# FT8 Native Decoder Changelog

This file tracks changes made specifically by the native OrcSDR FT8 decoder workstream. The main branch did not contain a repository-wide CHANGELOG.md when this workstream started.

## 2026-10-08 - JS8 front end on the real over-the-air capture; evidence tool and bound results

- `tools/js8-wav-front.cpp`: runs the same JS8 Normal backend as the firmware over a long 12 kHz WAV with a sliding 15 s window and reports sync
  candidates and raw 79-tone frames, optionally near reference frequencies; `--json` writes a machine-readable result. No AGC, no normalisation,
  no injected expected answers. `js8_native_backend` gained read-only `candidate()`/`candidate_count()` for it.
- Dataset: commit 1705472 of F:\AI\OrcSDR-TEMP\js8-pc-capture, `tests/fixtures/js8/corpus/2026-10-08`, checked with
  `tools/js8_capture/verify_dataset.py` (7 pairs, 870 standard-decoder runs, 5 unique frames).
- Result on `sample-40m-180s-002` (166 one-second-step windows): the front end reaches a raw frame for 3 of the 4 standard-decoder frames
  (635, 486 and 838 Hz); the 2604 Hz frame (reference -20 dB) is not reached because its sync candidates fail the demodulator's sync-hit check.
  All 16 raw frames sit near a reference frequency. The four zero-decode controls and the Slow capture give 0 raw frames. Saved with the WAV and
  IQ hashes and the decoder commit in `docs/js8/results/`. Tab5 acceptance is separate and still pending for real captures.

## 2026-10-08 - Opening the FT8 dashboard now tunes the radio

- `open_ft8_dashboard()` called nothing that tuned the receiver, so opening FT8 from Home left the radio on the Home station (for example FM at
  2.4 MS/s). The audio tap saw no block, the decoder sat in "listening" and Live showed no waterfall until a band button was tapped. Serial tests
  always sent a band command next, which hid the problem. The dashboard now calls `ft8_select_band(ft8_selected_band())` on open, which starts the
  runtime, tunes the selected band (240 kS/s for the decoder) and applies the mode's dial frequency.
- Checked on the Tab5: after boot, `FT8 OPEN` alone gives rx_running=1, tap=1, rate_hz=240000, blocks flowing, runtime=decoding, waterfall drawn.

## 2026-10-08 - JS8 firmware integration, step 3: runtime hook, serial diagnostics, JS8 dials (raw evidence only)

- `ft8_runtime` hosts the JS8 Normal backend beside the FT8/FT4 one. It is allocated in PSRAM (about 700 KB) the first time a JS8 mode is used and
  reuses the existing 12 kS/s audio tap and ring (no second IQ front end); FT8/FT4 and JS8 never run at the same time. `set_mode()` and
  `inject_run()` accept FT8, FT4 and JS8 Normal only; Fast, 40, Slow and 60 are refused and are never mapped to Normal. A JS8 slot logs
  `ORC_JS8_RT slot=... decodes=0 raw_frames=... candidates=... best_sync=... total_ms=... spectral=... search=... demod=...`.
- New serial commands (receive only): `JS8 STATUS | STATS | RAW | START | STOP | MODE NORMAL | BAND <label> | INJECT BEGIN|PING|RUN|<offset> <b64>`.
  `JS8 RAW` prints measured evidence (audio Hz, time offset, sync score, sync hits, margin), never text. `tools/tab5_ft8.py inject <wav> JS8` runs a recording.
- `mode_dial_hz()` returns the JS8 calling frequencies for every JS8 submode (40 m 7.078, 20 m 14.078 MHz and so on; 60 m and 2 m keep the FT8 dial
  until confirmed). The model self-check was updated to match.
- No `Decode` is produced; `finish_slot()` returns 0. The JS8 dashboard buttons stay disabled. No transmit capability.
- Memory: the JS8 path reuses the FT8 slot chunk buffer instead of adding a second 8 KB static (that second buffer boot-looped the device, see
  the previous entry). App size 4,072,672 bytes, +14,080 over the build without JS8, in the 6 MB partition.

## 2026-10-08 - Boot-loop fix (map arrays to PSRAM) and Live decode-list spacing

- The map's 16 KB of projection arrays in `ft8_dashboard.cpp` (`draw_world`) were static internal RAM. They now live in one PSRAM block
  allocated on first use. Internal RAM is the scarce resource on the Tab5: the largest internal DMA block after boot rose from about 36 KB
  to 38 KB with 52 KB free, and an 8 KB static added later by the JS8 runtime hook (see the JS8 entry) boot-looped the device until this
  headroom existed.
- The Live tab's "Latest decodes" list drew the UTC time over the SNR value at the larger font; the SNR, DT, DF and message columns moved right.

## 2026-10-08 — JS8 firmware integration, step 2: native JS8 receive backend (no decodes yet)

- New `js8_native_backend.{hpp,cpp}` (namespace `orcsdr::js8::native`), same lifecycle as the FT8/FT4 backend: `begin(12000, submode, Config, Memory)`, `begin_slot`, `offer_audio`, `finish_slot`, `stats`, `reset`. Large buffers (slot audio 360 KB, energy grid about 330 KB) come from the caller's allocator (PSRAM on the Tab5); no heap use while decoding.
- Pipeline: slot audio -> energy grid by one 1920-point FFT per half symbol (the proven FT8 FFT primitive; bins on the 6.25 Hz tone spacing, 200-3000 Hz) -> bounded JS8 sync search -> candidate-local demodulation to raw 79-tone frames. The FFT grid matches the exact-correlation oracle (`js8_spectral`) to 1.65e-5 of the strongest cell on identical PCM.
- Truth boundary kept: `finish_slot()` returns zero `Decode` records and `stats().decodes` is always 0. The tone-to-bit map, FEC graph, CRC and frame parser are not accepted yet; the acceptance layer attaches after the raw-frame stage. Only Normal is accepted: `begin()` and `set_submode()` refuse every other submode and never fall back.
- Tests: `tests/js8_native_backend_tests.cpp` (refusals, incomplete and short slots, noise-only slot gives no raw frame, synthetic frame recovers all 79 tones within 3.2 Hz and 45 ms, grid vs oracle), run under ASan/UBSan by `tools/test-ft8.sh`. Source added to the Tab5 build. Receive only; no UI change; FT8/FT4 untouched.

## 2026-10-08 — JS8 firmware integration, step 1: sources in the Tab5 build

- The eight device-relevant `js8_*.cpp` files (mode, frame, sync, spectral, demod, frontend, fec, snr) are now in `apps/orcsdr-tab5/main/CMakeLists.txt`. Host-only reconstruction tools stay out of the firmware. The firmware builds (ESP-IDF 5.5.4); the app binary is 4,058,592 bytes both before and after, because nothing references the JS8 code yet and the linker discards it. The real size cost will be measured when the backend lands. Receive-only; no decoder behaviour change; FT8/FT4 untouched.

## 2026-10-08 — Decode SNR (receive-only estimate, calibrated against signals of known strength)

- Decodes now carry an SNR in the usual weak-signal convention (signal power over noise power in 2500 Hz), shown in the DECODES table, the `ORC_FT8_DECODE` serial line (`snr=`) and a new `snr_db` column in the decode log. It replaces the dash and the `decode_flag_snr_unavailable` flag for accepted decodes. This lifts the earlier "no SNR" rule at the owner's request; no number is shown for a candidate that failed the gates.
- Method (`ft8_snr.{hpp,cpp}`, clean-room): the CRC-valid message is re-encoded, so the transmitted tone of every data symbol is known; signal is the energy at that tone minus noise, noise is a low percentile of guard bins 8 to 14 tone widths outside the occupied band (robust to neighbouring stations), with the grid's own sidelobe leakage removed. Floor -30 dB, never a smaller number.
- Calibration: `tools/ft8-snr-sweep.cpp` synthesises FT8/FT4 at exactly known SNR in Gaussian noise and runs it through the real backend. Mean error against truth: FT8 within about 1 dB from -16 to +12 dB (sd 0.5-0.9 dB); FT4 within 0.4 dB from -12 to +12 dB. Decode threshold on this white-noise test: FT8 about -18 to -20 dB, FT4 about -12 dB. `tools/test-ft8.sh` now runs a `--check` of the sweep.
- Against WSJT-X on the official FT8 recording the estimate agrees on one message (W1FC +15 vs +15) and differs by -5 to +8 dB on the others (median about -3 dB). WSJT-X's numbers are its own estimate on real, non-white signals, not ground truth; the synthetic sweep is the calibration. Official FT4 file: -2 to -8 dB low on three messages. These differences are recorded here rather than tuned away.
- Advanced: `FT8 SNR [OFFSET <dB>|RESET]` adds a user trim (stored in NVS, default 0). A Settings control for it is still to do. Not yet verified live on the Tab5: the RTL-SDR was not detected at boot after the flash.

## 2026-10-08 — Decode log to the SD card works; internal DMA memory recovered

- `FT8 LOG [ON|OFF|FLUSH]` and `FT8 MEM [FULL]` (heap budget). Every decode is a CSV row staged in PSRAM and written to `/orcsdr/ft8/log-YYYYMMDD.csv` (one file per UTC day, 4 MB cap) when internal DMA memory allows (4 KB largest block). On by default, stored in NVS. Stations heard, never contacts. Verified on hardware: 34 rows written while the receiver ran, pulled back with `SD_GET`. ADIF and SAVE files moved to `/orcsdr/ft8/` too, because `SD_GET` only serves `/orcsdr/`.
- Cause of the SD failures, found by logging the heap at each boot stage: the largest internal DMA block falls from 27 KB to 3 KB at `ORCDIAL_V4_BRIDGE_READY`, about 1.5 s after the receiver starts on Home entry. It is not FT8 and not the receiver. The OrcDial bridge start takes about 24 KB of internal RAM: the secure runtime's 12 KB task stack, its three queues (about 5 KB), the bridge's two frame queues and a 4 KB transmit task stack. The bridge and secure-runtime queues now live in PSRAM (`xQueueCreateWithCaps`, internal fallback; `orcdial/src/control/secure_runtime.hpp` change is guarded for the ESP32-P4 only). Largest DMA block after boot is now 10-11 KB (was 3 KB), also with FT8 running.
- The storage wrapper's `open()` did not support `FILE_APPEND`: an append open was treated as a read of a missing file. It now maps to `"ab"`. (The one live log that used it, LoRa, is affected the same way.)
- Measurement aids: `RTL_DMA_WATCH` lines (largest DMA block moving 4 KB or more) and `RTL_DRAM_BUDGET` at receiver start and OrcDial bridge steps. An earlier note here blamed the receiver and the FT8 runtime; that was wrong.
- The Tab5 main loop stopped answering serial/touch twice during this work while the decoder task kept running; it did not recur in about 4 hours of later runs, cause unknown.

## 2026-10-08 — Smoother Live waterfall, flicker-free MAP, NEW markers

- Live waterfall: the whole 848x224 area was repainted every 400 ms (about 2.5 frames a second). It now scrolls the old picture and draws only the new rows (about 10 ms a paint). The runtime emits one row per 70 ms of audio even when the tap hands audio over in bursts, and the dashboard spends them at a steady pace from the main loop. Measured on the Tab5: 8 rows/s before, 14.3 rows/s after; row computation costs 2 ms.
- MAP: composed off-screen and pushed in one go, and a new grid no longer blanks the body first, so it no longer flashes. The map always shows the whole world (owner preference); zoom/pan can be added to the existing view struct.
- DECODES: a NEW badge marks a callsign's first appearance in the session store (`decode_flag_new_station`, set by `DecodeStore::append`, host-tested); column spacing reworked for the larger text; bearing shown as degrees.

## 2026-10-08 — MAP basemap restyled to the OrcMaps dark theme

- The MAP outline looked nothing like the OrcMaps maps. It now fills land (slate on navy water, thin borders) with the colours of the OrcMaps `world-orcsdr-dark` render, using an even-odd scanline fill of the Natural Earth 110m polygons. This is a style match only: the FT8 tab still does not use the OrcMaps engine or tiles (that integration lives on `claude/orcmaps-integration` and the app is near the size guard there).
- Recent-grids rows and the Conditions panel were re-spaced for the larger text; singular/plural fixed.

## 2026-10-08 — World outline on MAP, larger text

- MAP now draws an offline world outline (coasts and country borders from Natural Earth 1:110m, public domain, about 15 KB) generated by `tools/gen_ft8_world.py` into `ft8_world_data.cpp`. It shows the whole world until a decoded station has a grid, then frames the receiver and stations (minimum 60 x 30 degrees).
- Dashboard labels that were DejaVu18 are now DejaVu24; dense captions (stat-card titles, mode-button sublabels) stay at DejaVu18. Hunter cards drop the slot count to fit. Setup mode buttons and footer no longer overflow. Checked on hardware.

## 2026-10-08 — Larger dashboard text

- The FT8/FT4 dashboards drew small labels in the 6x8 built-in font, unreadable on the 1280x720 panel. Size-1 text (about 30 labels: hunter cards, table captions, map/heard hints, setup footer) now draws DejaVu18. Checked on hardware on every tab (DECODES, MAP, HUNTER, HEARD, SETUP); nothing overflowed.

## 2026-10-08 — Live cross-check against PSKReporter

- First live FT4 decode on 20 m (14.080 MHz): `W9DHI KE5YYC R-01`; PSKReporter shows KE5YYC transmitting FT4 at about 14.08148 MHz in the same minute.
- Frequency accuracy, 20 m FT8 on the Blog V4 + MLA-30+: 11 decodes matched to PSKReporter spots of the same sender within 20 s read a median of +27 Hz (about 2 ppm, mostly +26 to +41 Hz; one pair with only two spots read -10 Hz). The same decoder is within 3 Hz of WSJT-X on the official recording, so the offset belongs to the dongle clock, not the decoder. One FT4 pair read -135 Hz; one sample, not conclusive. No correction applied (setup-specific).
- PSKReporter's query API rate-limits quickly (about 15 rapid queries); use one bulk query per few minutes.

## 2026-10-08 — On-device regression by serial injection

- Added `FT8 INJECT BEGIN|PING|RUN|<offset> <b64>` (authenticated) and `tools/tab5_ft8.py inject <wav> FT8|FT4`: a 12 kHz recording is uploaded (CRC-verified, re-pairs when the 5 s session lapses) and decoded by the real backend on the Tab5 as one slot. Test-only; receive-only, no transmit path.
- USB Serial/JTAG console receive buffer raised from 1 KB to 8 KB; long scripted lines overflowed it.
- Tab5 result on the official recordings matches the host build: FT8 `210703_133430.wav` 7 of 14 messages in 3.7 s; FT4 `000000_000002.wav` 3 of 19 in 1.1 s; no false accepts.

## 2026-10-07 — Phase 0 research

- Created the native FT8 decoder research branch from current OrcSDR main.
- Added DECODER_RESEARCH.md covering the current FT8 protocol, existing decoder strategies, embedded constraints, ESP32-P4/Tab5 capabilities, clean-room provenance, proposed OrcSDR architecture, risks, and benchmark plan.
- Recorded that the DecoderBackend and Decode UI seam is currently on the separate FT8 UI sandbox branch rather than main; no UI work was copied into this branch.
- Established a Phase 0 stop gate: no decoder implementation begins until the owner approves the research decisions.
- No firmware/runtime code, transmit code, DSP changes, or hardware-validation claims were added.


## 2026-10-07 — Phase 1 slice 1: protocol primitives

- Owner approved the Phase 0 workflow; implementation remains confined to `codex/ft8-native-decoder-research` and draft PR #172.
- Added a pure C++17 FT8 protocol core with no ESP-IDF dependencies: protocol sizes, CRC-14, standard callsign c28 encoding, 10/12/22-bit callsign hashes, FT8 Gray tone mapping, and Costas/data-symbol framing.
- Added host regression runners for Linux/WSL and PowerShell plus optimized and ASan/UBSan coverage.
- Cross-checked the independent implementation against the QEX authors' explicitly public-domain helper resources: 100 CRC vectors, 100 callsign-hash vectors, and 100 standard-callsign encodings; 300/300 matched.
- Added `docs/ft8/PHASE1_IMPLEMENTATION.md` as the implementation/provenance notebook.
- This slice is not wired into the Tab5 firmware build, changes no receiver DSP, adds no transmit path, and makes no hardware-verification claim.


## 2026-10-07 — Phase 1 slice 2: LDPC definition and correctness

- Added a pure C++ FT8 LDPC(174,91) systematic encoder and sparse parity/syndrome checker.
- Stored compact protocol constants generated from the QEX authors' explicitly public-domain `generator.dat` and `parity.dat` resources.
- Cross-checked the generator matrix against the independently supplied parity-check matrix using all 91 message basis vectors and 1,000 deterministic pseudo-random messages.
- Verified every one of the 174 single-bit codeword corruptions produces a nonzero syndrome of weight three, matching the published column weight.
- Added a fixed public-matrix-derived parity vector and extended optimized + ASan/UBSan host CI.
- No soft LDPC decoder, RF/DSP integration, firmware build integration, transmit path, or hardware-validation claim is included in this slice.


## 2026-10-07 — Phase 1 slice 3: normalized-min-sum LDPC decoder

- Added a pure C++ normalized-min-sum LDPC soft decoder with fixed-size caller-owned scratch and early syndrome termination.
- Refactored the public-domain FT8 sparse parity graph into one shared internal definition; NMS check adjacency is derived at compile time.
- Added deterministic optimized + ASan/UBSan tests including the 255-iteration wrap guard.
- Added a reproducible BPSK/AWGN normalization sweep; alpha 0.80 is the provisional synthetic-channel baseline, subject to re-test with real FT8 tone-derived LLRs.
- Documented that LDPC convergence is not a valid FT8 decode; CRC and legal message plausibility remain mandatory downstream gates.
- Host workspace measured 4,872 bytes; no Tab5/P4 performance or hardware-verification claim is made.


## 2026-10-07 — expanded Phase 0: FT8 / FT4 / JS8 and integrated UI baseline

- Preserved the pre-rebase decoder checkpoint at `codex/ft8-native-decoder-pre-ui-rebase`.
- Rebased/reconstructed the decoder workstream on `claude/ft8-ui` and retargeted draft PR #172 to that branch; `main` remains untouched.
- Merged the UI branch's FT8 model/Hunter/OrcDial host tests with the native codec/LDPC/NMS regression runner. Documentation Truth, FT8 Native Decoder Core, and FT8 RX core CI all passed on the integrated checkpoint.
- Expanded research from FT8-only to a ModeProfile framework for FT8, FT4, and JS8.
- Verified FT4's 7.5 s period, 576 samples/symbol at 12 kHz, 20.8333 baud/tone spacing, 105-symbol framing, 83.3 Hz nominal bandwidth, shared LDPC(174,91)/CRC-14 family, four Costas arrays, and FT4 payload XOR transform from primary WSJT-X/QEX sources.
- Recorded stable JS8 Slow/Normal/Fast/JS8 40 profile timing and framing behavior from the current JS8Call guide, with source-only protocol research confirming the 75-bit + CRC-12 / N=174,K=87 frame family. JS8 60 remains experimental/disabled.
- Added a backwards-compatible decoder seam proposal: optional mode selection, millisecond slot timing, mode/provenance tags, and later bounded JS8 message assembly output.
- Audited the current receive/audio path and proposed an independent raw-CU8-to-12-kHz USB analysis sidecar. Existing AM/SSB/speaker audio is not reused as the decoder signal.
- Added a tap-off bit-identity requirement and on-device CPU/drop A/B gates. No existing DSP, demodulation, filter, sound, dashboard, Hunter, OrcDial, or main.cpp glue was changed.
- Froze further Phase 1 implementation until the owner reviews the expanded research and proposals.


## 2026-10-07 — Phase 1 slice 4: ModeProfile foundation

- Owner approved moving forward with the expanded FT8/FT4/JS8 decoder architecture.
- Added pure C++ `orcsdr::ftx::ModeProfile` definitions for FT8, FT4, and descriptive JS8 submodes.
- Marked FT8 and FT4 implementation-ready; JS8 profiles remain research-pending/experimental until missing clean-room sync/FEC definitions are independently established.
- Added exact FT8 and FT4 timing, tone, frame, Costas, FEC-family and payload-transform metadata.
- Added optimized + ASan/UBSan host tests and CI coverage.
- No UI-owned files, existing DSP/demod/audio path, main.cpp glue, transmit path, or firmware binding were changed.


## 2026-10-07 — Phase 1 slice 5: shared sync candidate scorer

- Added a pure C++ ModeProfile-driven synchronization scorer over an abstract spectral-energy grid.
- Added configurable time/frequency oversampling geometry so the search is not tied to one FFT layout.
- Added local competing-tone normalization, bounded candidate ranking, and non-maximum suppression without heap allocation.
- The sync score is documented as a dimensionless contrast metric, not SNR.
- FT8 and FT4 synthetic sync searches pass optimized + ASan/UBSan tests; JS8 research-pending profiles are explicitly refused.
- No FFT/channelizer, firmware binding, UI-owned file, existing audio path, or transmit code was changed.


## 2026-10-07 — Phase 1 slice 6: shared soft demodulation

- Extended ModeProfile with protocol-defined data blocks and tone-to-bit Gray labels.
- Added pure C++ candidate-local soft demodulation shared by FT8 and FT4.
- The demodulator emits exactly 174 dimensionless max-log-style LLRs with the same sign convention as the NMS decoder.
- Deterministic FT8 and FT4 tests verify all 174 soft-bit signs; equal-tone input produces zero reliability.
- JS8 remains disabled/research-pending.
- No FFT/channelizer, firmware binding, UI-owned file, existing audio path, or transmit code was changed.


## 2026-10-07 — Phase 1 slice 7: incremental spectral reference

- Added a pure C++ streaming 12 kHz exact-correlation spectral reference backend with caller-owned output and fixed workspace.
- Added irregular-chunk streaming tests and exact-tone peak validation.
- Added the first end-to-end synthetic FT8 coded-frame recovery test: PCM -> spectral grid -> Costas sync -> soft demod -> NMS LDPC -> CRC.
- The recovered 91-bit message and 174-bit codeword must exactly match the injected test fixture.
- This is not yet a user-visible FT8 message decode; message unpack/plausibility remains mandatory.
- Documented future conducted RF verification using an external Pluto-compatible signal source over coax/attenuation while OrcSDR itself remains RX-only.
- No firmware audio tap, UI-owned file, existing receive DSP, PTT, CAT, or transmit path was added.


## 2026-10-07 — external receive-validation plan

- Documented official WSJT-X FT8/FT4 receive WAVs as external benchmark inputs.
- Defined a three-layer owner-controlled corpus: raw RTL CU8 IQ, 12 kHz analysis PCM, and decoder result logs.
- Defined conducted Pluto-compatible RF replay over coax/attenuation as the preferred full-chain hardware test while OrcSDR remains strictly RX-only.
- Third-party WAVs remain external unless redistribution rights are explicit; committed real recordings should be owner-controlled.


## 2026-10-07 — Phase 1 slice 8: CRC-gated receive pipeline

- Added a heap-free internal receive pipeline connecting sync search, soft demodulation, NMS LDPC and CRC-14.
- Added a negative test proving a valid LDPC codeword with invalid CRC is not returned.
- Kept UI Decode output blocked until source-message unpack and plausibility validation exist.
- FT4 remains intentionally blocked at the final frame gate until payload XOR restoration is implemented and tested.
- No firmware binding, existing audio/DSP change, PTT, CAT or transmit functionality was added.


## 2026-10-07 — Phase 1 slice 9: standard message unpacking

- Added conservative QEX Type 1/2 source-message unpacking.
- Added inverse c28 decoding with canonical round-trip validation.
- Added g15 grid/report/acknowledgement decoding and standard text rendering.
- Recognized but did not invent text for unresolved 22-bit hashes.
- Unsupported CQ modifiers and other i3 message families remain rejected until independently implemented.
- Added optimized + sanitizer host-test coverage through the FT8 test runner.


## 2026-10-07 — Phase 1 slice 10: first full synthetic FT8 receive decode

- Added source-message plausibility to the pipeline after LDPC and CRC.
- Only fully renderable supported standard messages leave the current pipeline.
- Added a CRC-valid but unsupported-i3 negative test.
- Replaced the synthetic random-payload PCM test with a complete standard FT8 message fixture: `CQ K1ABC FN42`.
- The end-to-end host test now spans PCM -> spectral analysis -> sync -> soft demod -> NMS LDPC -> CRC -> standard message text.
- This remains host-side synthetic receive validation; it is not Tab5/RF hardware verification and adds no transmit capability.


## 2026-10-07 — Slice 10 focused review correction

- Fixed one missing closing brace in the rewritten full-decode spectral test fixture; decoder production modules were unaffected.
- Ran a focused optimized + ASan/UBSan sandbox test of the new pipeline plausibility gate.
- Verified `CQ K1ABC FN42` is accepted/rendered and an unsupported CRC-valid i3 family is rejected.
- Kept the distinction between focused sandbox validation and a full newest-head branch regression run explicit.


## 2026-10-07 — host real-WAV benchmark runner

- Added `tools/ft8-wav-benchmark.cpp` and `tools/benchmark-ft8-wav.sh`.
- The host runner accepts 12 kHz mono i16 PCM WAVs and drives the actual native receive pipeline through plausibility-gated text.
- Search resolution is configurable without changing decoder code.
- Output keeps sync/contrast metrics dimensionless; no uncalibrated SNR claim is made.
- The official WSJT-X busy-band WAV remains an external benchmark target; no decode result is claimed until the exact file is successfully run.


## 2026-10-07 — full decoder host checkpoint validation

- Temporarily enabled the decoder branch in the native-core workflow push trigger to force validation of the newest GitHub-App commit chain.
- GitHub Actions run 37683738040 passed `bash tools/test-ft8.sh` at commit `8d03508c`.
- Confirmed the complete ModeProfile/spectral/sync/demod/pipeline/message/codec/LDPC/NMS + UI/Hunter/OrcDial optimized and ASan/UBSan suite passes.
- Removed the temporary branch push trigger immediately after validation; normal push scope returns to main.


## 2026-10-07 — rebase onto multi-mode UI seam and real-WAV baseline

- Rebased the native decoder workstream onto `claude/ft8-ui` commit `be87fe47edbfe4e06d4c6794ca7c13a1bc7498fe`.
- Preserved the UI-owned DigitalMode/Decode/backend seam, dashboard/model/Hunter, OrcDial, and main.cpp integration unchanged.
- Merged `tools/test-ft8.sh` so the UI backend seam tests and all native decoder tests run together.
- Recorded the official WSJT-X `210703_133430.wav` baseline using the installed `jt9` executable as the external reference: WSJT-X reports 14 messages while OrcSDR accepts 1 at the default 80 ms / 6.25 Hz search, with zero accepted messages outside the reference list.
- Added a CI regression floor requiring at least one decode and the known `WM3PEN EA6VQ -09` message before any coverage optimization is accepted.


## 2026-10-07 — FT4 receive payload restoration gate

- Added the FT4 receive-side 77-bit XOR restoration using the pseudo-random sequence printed directly in the QEX protocol paper.
- The transform is applied only after LDPC convergence and CRC-14 validation, before source-message unpacking.
- Enabled FT4 in the shared CRC/plausibility pipeline for supported standard-message families.
- Added an end-to-end synthetic FT4 energy-grid test proving sync -> soft demod -> LDPC -> CRC -> un-XOR -> standard message rendering for `CQ K1ABC FN42`.
- No transmitter, waveform output, PTT, CAT, or firmware binding was added.


## 2026-10-07 — mode-selectable real-WAV benchmark

- Extended the host WAV benchmark to select FT8 or FT4 while using the same shared spectral/sync/demod/pipeline implementation.
- Extended external CI to download the official WSJT-X FT4 tutorial sample, run `jt9 --ft4 -d 3` as the external reference, and run OrcSDR in FT4 mode on the same recording.
- No FT4 regression floor is set until the first real measurement establishes an honest baseline.


## 2026-10-07 — FT4 external sample correction

- The older user-guide FT4 tutorial filename `200514_182053.wav` currently returns HTTP 404 from the public SourceForge sample path.
- Switched the reproducible external benchmark to currently published official WSJT Project FT4 sample `190106_000115.wav`.
- Both WSJT-X `jt9 --ft4 -d 3` and OrcSDR consume the same downloaded WAV; no third-party sample bytes are committed to the repository.


## 2026-10-07 — frozen real-recording baselines

- Measured the current installed WSJT-X `jt9` executable against the exact same external WAV bytes used by OrcSDR.
- FT8 `210703_133430.wav`: WSJT-X 14 versus OrcSDR 1, 7.1% coverage, zero observed false accepts, 4.05 s regression-floor host wall time (5.15-5.94 s on earlier equivalent hosted runs).
- FT4 `000000_000002.wav`: WSJT-X 19 versus OrcSDR 2, 10.5% coverage, zero observed false accepts, 1.90 s regression-floor host wall time (2.29 s on the prior survey).
- Added explicit FT8 and FT4 external-WAV CI floors for the currently stable accepted messages.
- OrcSDR SNR remains unimplemented and is not fabricated in benchmark output.

## 2026-10-07 — Task 3 experiment 1: search-geometry measurement and miss classification

- Instrumented the receive chain without changing what it accepts: `pipeline::try_candidate` (with `Outcome` and `CandidateTrace`) is now the single
  per-candidate path, and `decode_grid` calls it, so production and diagnostics cannot diverge.
- Added the host tools `tools/ft8-wav-diagnose.cpp` / `tools/diagnose-ft8-wav.sh` and the WSJT-X reference lists in `tools/ft8-reference/`. Every reference
  signal is classified (decoded, no sync candidate, suppressed by NMS, ranked beyond the limit, demod/LDPC/CRC failure, unsupported message, out of analysis
  band, starts before the recording, truncated by the end), with per-stage candidate and LDPC/CRC counts and timings. The shared WAV reader moved to `tools/ft8_wav_common.hpp`.
- Measured FT8 and FT4 at 2,1 / 4,1 / 4,2 on the official recordings: FT8 1 to 2 to 5 accepted (6 with the limit lifted), FT4 2 to 2 to 3, zero false accepts, and zero accepts on 48
  deterministic noise runs. Results and diagnosis are in `REAL_WAV_BENCHMARK.md`.
- Found and documented (not yet fixed): the standard-callsign encoder rejects callsigns with more than one digit (`A92EE`), and FT4 contest message types are unparsed.
- No decoder math, regression floor, firmware, UI or audio path changed.

## 2026-10-07 — Task 3 experiment 2: candidate-local time/frequency refinement (host-only measurement)

- Added `tools/ft8-wav-refine.cpp` (host only, exact correlation): coarse 2,1 search, per-candidate sample/sub-bin refinement scored on sync symbols, then the unchanged `pipeline::try_candidate`. Includes a `--no-refine` control.
- Measured: FT8 official recording 1 to 6 accepted (K=64) versus the unrefined control, equal to the 4,2 grid at about a quarter of its spectral cost; FT4 2 to 3. Zero false accepts on both recordings and on 16 noise recordings.
- No production decoder code, acceptance rule, firmware, UI or audio path changed. Not an ESP32-P4 timing.

## 2026-10-07 — Task 3 experiment 3: candidate capacity and refined re-ranking (host-only)

- `ft8-wav-refine` gained `--gate N`: refine K candidates, rank by refined sync score, attempt the best N. FT8 reaches 6 and FT4 3 with K=64 and only 16 gate attempts; larger K gives no further gain on the official recordings. Zero false accepts, zero accepts on 16 noise recordings. No production code changed.

## 2026-10-07 — Task 3 experiment 4: soft metrics measured, no gain

- Added `demod::Metric` (default `linear_symbol`, production behaviour unchanged) and the host options `--metric`, `--gain`, `--iters`, `--norm`, `--oracle` in `ft8-wav-refine`. Amplitude, frame-normalized and log-sum-exp metrics and 12 LDPC settings were measured: none beats the current metric (FT8 6, FT4 3). An oracle-position test shows 7 of 13 FT8 signals decode at their true position, 1 is lost to the callsign encoder bug, 5 are too weak for this demodulator.

## 2026-10-07 — Fix: standard callsigns whose prefix contains a digit (A92EE)

- `codec::encode_standard_callsign` assumed exactly one digit in the call, so valid calls whose two-character prefix contains a digit (`A92EE`) failed the canonicality round trip and a CRC-valid FT8 decode was discarded. The digit position now follows the packing layout (third character if a digit, else second). Measured alone on the official recording with the refined pipeline (K=64, G=16): FT8 6 to 7 accepted (`A92EE F5PSR -14` added), FT4 unchanged at 3, zero false accepts, zero accepts on 16 noise recordings. Unit tests updated: `K12ABC` is now (correctly) encodable; `AEEE` and `AB123` stay rejected.

## 2026-10-07 — Refinement search shape (host-only)

- `ft8-wav-refine`: added `--joint`, `--rounds`, `--watch`. A joint time-frequency scan recovers `XE2X HA2NP RR73` that sequential coordinate refinement misses (local optimum): FT8 7 to 8 accepted on the official recording at K=64, FT4 unchanged at 3, zero false accepts, zero accepts on 16 noise recordings. Lowering the coarse sync threshold and widening the coarse list (K up to 256) changed nothing. Host-only measurement; no production code changed in this entry.

## 2026-10-08 — Native FT8/FT4 decoder bound to the Tab5 firmware (receive only)

- Added `ft8_audio_tap` (raw-CU8 sidecar to 12 kS/s USB audio), `ft8_spectral_fft` (float mixed-radix FFT matching the exact oracle), `ft8_native_backend`
  (fine-grid refinement design), `ft8_runtime` (PSRAM ring, UTC slot scheduler, decoder task) and the `FT8 ...` serial command suite with `tools/tab5_ft8.py`.
- Measured on the Tab5: about 3.6 s of decode per 15 s FT8 slot and about 3.4 ms of tap per 6.8 ms IQ block; first real decodes (20 in about 3.5 minutes on 40 m).
  Details, the dial-offset and clock findings, and the gain finding are in `NATIVE_BINDING.md`.
- No transmit path was added. SNR is not reported (no calibrated estimator).

## 2026-10-08 — Boot clock alignment, per-mode dial frequencies, corrected gain finding

- `time_service::initialize` waits for the hardware RTC's seconds to tick before taking the system time, so a cold boot lands within milliseconds of the RTC instead of up to a second behind (measured: -14 ms against the PC after a cold boot, was -2.5 s). Costs at most about 1.1 s at boot.
- `mode_dial_hz()`: FT4 has its own dial frequencies (e.g. 40 m 7.0475 MHz, 20 m 14.080 MHz); changing mode retunes, and the dashboard, serial status and OrcDial packet show the active mode's dial. FT4 runs on the device (7.5 s slots, about 1.5 s decode) but has not yet decoded a live signal.
- Corrected `NATIVE_BINDING.md`: gain was not the cause of the early no-decode result (a dial-offset bug was); decodes appear at every manual gain from 0 to 45 dB and the tap level does not change, so no gain policy is claimed.

## 2026-10-08 — Gain: no policy needed (measured)

- `FT8 STATUS` gains `iq_dbfs`, `iq_clip_pct`, `gain_tenth_db`. A manual-gain sweep showed the decoder's slot level rising to a knee near 34 dB (with 9 to 30 percent IQ clipping beyond it) and an alternating A/B of the receiver's AUTO setting against manual 34 dB gave 6.55 against 6.64 decodes per slot over 22 slots each: AUTO already sits at the knee, so no FT8 gain policy was added. A hill-climbing gain seeker was built and removed. `ft8_native_service()` (called from `loop()`) now only hands decodes to the store so a headless run stays current.

## 2026-10-08 — Conditions panel on the HEARD tab; ADIF export; FT8 TAB

- The HEARD tab shows a CONDITIONS panel from the decodes and the saved receiver location (`ft8_conditions`): stations with a grid, farthest station with distance and bearing, median distance and decodes per compass sector; it prompts for a location when none is set.
- `FT8 ADIF <name>` writes the decode list to `/sd/ft8/<name>.adi` using `ft8_adif` (verified: 64 records written on the device). `FT8 TAB <name>` switches the dashboard tab for scripting.

## 2026-10-08 — Stack-fault fix, FT8 SHOT and a screenshot helper

- Fixed a stack protection fault on core 0 when the FT8 screen opened while the decoder ran: the 64-decode dashboard snapshot (about 8 KB) is now filled in place in a static instead of being built on the main task stack and copied, and the serial-command buffers are static. Verified: the screen opens during decoding with no panic and slots keep decoding.
- `FT8 SHOT <name>` saves the screen to the SD card; `tools/tab5_ft8_shot.py` fetches it (the receiver is stopped for the SD transfer and retuned afterwards). Verified HEARD (Conditions panel) and DECODES (GRID/DIST/BRG columns) on the device.

## 2026-10-08 — Live waterfall on the LIVE tab

- Replaced "WATERFALL INPUT PENDING DSP BINDING" with a live waterfall from the tap audio (`ft8_runtime::waterfall()`, a low-priority task, 1920-point FFT every 150 ms, rows normalised to their own median). The panel takes byte-swapped RGB565 through `pushImage`, so the palette is swapped. Repaints are limited to every 400 ms. The GAIN chip shows `AUTO` only, and the SETUP footer was updated. Verified by screen capture on the device.

## 2026-10-08 — Band hunter bound to the decoder; 6 m and 2 m tuning fixed

- The HUNTER tab and `FT8 HUNT <FAST|DECODE|STOP>` / `FT8 HUNTSTATUS` run `ft8_hunter` against the live decoder (`ft8_hunt_service()` in the loop). Added `strong_candidates` to the decoder stats (clear sync, refined score 0.45 or more) so a band is only called "FT8 SIG" for real sync, not for the 64 weakest-passing candidates.
- Fixed: bands above 30 MHz (6 m, 2 m) were clamped to 30 MHz by the shortwave path. They now tune through the general VHF band, and the audio tap runs on both paths.
- Measured: fast hunt visits 10 bands in about 70 s; decode hunt about 10 to 12 minutes; 40 m was best (12 decodes) in the first run.

## 2026-10-08 — 6 MB app partition, NTP aligned to the second, clock finding corrected

- Brought the 6 MB app partition onto this branch (cherry-picked from `claude/app-partition-6m`): the app is 0x3d6410 bytes with 36 percent (2.2 MB) free instead of 4 percent. Verified on the Tab5: it boots on the new table (a one-time full reflash) and still decodes.
- `ntp_sync` now writes the RTC exactly on a whole-second boundary (it wrote mid-second, leaving the RTC off by up to a second). `FT8 NTP` and one automatic sync per boot when the FT8 decoder starts with Wi-Fi up.
- Corrected the clock/DT finding: the PC clock used as a reference was 0.46 s ahead of true time, which produced the constant +0.58 s DT. With an NTP-corrected Tab5 clock the decoded DT centres on zero (median -100 ms, mean -29 ms, 59 decodes).

## 2026-10-08 — FT8 asks the driver for 240 kS/s

- Selecting an FT8 band now sets the receiver rate override to 240 kS/s (the driver's low band is 225 to 300 kS/s) and restarts the stream if it runs at another rate, instead of 2.4 MS/s. DSP load fell from 67 to 19 percent on the Tab5 and decoding is unchanged. The DSP task lets the tap see these blocks (they are flagged as a non-default rate). Band selection now starts the decoder runtime first, so the rate override is not released before the stream restarts. When the decoder stops the override is cleared and a stream still at 240 kS/s on a dashboard band is restarted at its normal rate (verified: back to 2.4 MS/s).

## 2026-10-08 — MAP tab: receiver marker and fitted view

- The MAP tab marks the receiver (white ring and cross, "YOU") from the saved location and frames the receiver plus the decoded stations with a margin (world view when there is nothing to frame), with grid lines at a step chosen for about six columns. Verified by screen capture on the device.

## 2026-10-08 — A completed hunt tunes its best band

- When a band hunt completes the receiver now tunes to the best band it found (it used to stay on the last band visited). Verified: a fast hunt ended on 40 m. A decode hunt at 12:50 UTC on the Tab5 (V4 plus MLA-30+): 40 m best with 8 decodes, 80 m 2 decodes, 10 m FT8-like sync without a decode, 160 m and 30 m through 12 m and 6 m quiet; the HUNTER tab renders these results (screen capture).


## 2026-10-08 — JS8 receive reconstruction slice 1

- Added a separate `orcsdr::js8` module with JS8 submode profiles and raw 79-tone frame handling; no FT8/FT4 decoder behavior changed.
- Established the Normal sync pattern from the public JS8Call API `TX.FRAME` tone vector and added extraction of the two 29-symbol data blocks (58 data tones total).
- Added `docs/js8/PROTOCOL_FACTS.md`, `RECONSTRUCTION.md`, `INTEGRATION.md`, `STATUS.md`, and fixture provenance scaffolding.
- Exact tone-bit mapping, FEC, CRC and upper-layer packing remain explicitly blocked pending independent reconstruction from owner/reference captures; the module cannot emit a user-visible JS8 decode yet.
- Added optimized and ASan/UBSan JS8 host suites to `tools/test-ft8.sh`. No firmware binding, UI, transmit path, PTT, CAT or flashing work was added.


## 2026-10-08 — JS8 FEC reconstruction tooling

- Added host-only `tools/js8-gf2.hpp` and `tools/js8-fec-reconstruct.cpp` to derive a binary code-space rank and orthogonal parity-check basis from observed 174-bit JS8 channel words.
- Added a deterministic synthetic-subspace test, including rejection of a vector outside the learned code space; optimized and ASan/UBSan runs pass in the sandbox.
- The tool contains no copied JS8 FEC matrix and makes no claim that JS8's FEC has been recovered yet. Real use is blocked on captured frames and an independently established tone-to-bit mapping.
- No firmware binding, UI, shared FT8/FT4 decoder behavior, transmit path, PTT, CAT or flashing work changed.


## 2026-10-08 — JS8 tone-label reconstruction search

- Added host-only `js8-tone-map`: it consumes verified Normal 79-tone frames, removes the measured sync symbols, tests all 8! tone-to-three-bit label permutations, and ranks each resulting 174-bit corpus over GF(2).
- Added deterministic tests for tone-label validation, bit ordering and mapped-rank calculation; optimized and ASan/UBSan runs pass in the sandbox.
- The tool deliberately does not hard-code a JS8 tone/Gray map or expected FEC rank as a decoding fact. It warns when the corpus is too small to distinguish a suspected 87-dimensional code from arbitrary mappings.
- No firmware binding, UI, shared FT8/FT4 decoder behavior, transmit path, PTT, CAT or flashing work changed.


## 2026-10-08 — JS8 Normal candidate demod primitive

- Added separate `js8_demod.*`: an allocation-free, float-only candidate-local 12 kHz PCM demodulator that evaluates all eight tones for each of 79 Normal symbols and reports raw tones, sync hits, sync contrast and winner margin.
- Added a host-only synthetic tone fixture based on the public API frame vector. It recovers all 79 tones with deterministic noise and rejects a deliberately wrong base-frequency candidate; optimized and ASan/UBSan tests pass in the sandbox.
- This is a receiver primitive, not a JS8 message decoder: no whole-band search, FEC, CRC, message acceptance, SNR calibration or firmware binding exists yet.
- No UI, shared FT8/FT4 decoder behavior, PTT, CAT or flashing work changed.


## 2026-10-08 — JS8 Normal bounded sync search

- Added standalone `js8_sync.*`: heap-free Normal-mode sync scoring and bounded non-maximum-suppressed candidate search over a caller-provided spectral energy grid.
- Synthetic host tests recover the injected Normal candidate, reject a flat grid at the test threshold, and refuse Fast while its exact sync pattern remains unverified; optimized and ASan/UBSan runs pass in the sandbox.
- Sync evidence is not a decode. FEC, CRC and frame acceptance remain unavailable pending reconstruction.
- No firmware binding, UI, shared FT8/FT4 decoder behavior, PTT, CAT or flashing work changed.


## 2026-10-08 — JS8 exact-correlation spectral oracle

- Added standalone `js8_spectral.*`, an allocation-free single-precision PCM-to-energy-grid reference front end for the JS8 receive module.
- Added a noisy synthetic Normal-mode test that recovers the injected sync candidate at the exact expected time/frequency bin and a quiet control that produces no candidate; optimized and ASan/UBSan versions pass in the sandbox.
- Documented this as a correctness oracle rather than the final P4 full-band implementation; an optimized FFT/coarse-search front end must be measured against it before device binding.
- No firmware binding, UI, shared FT8/FT4 decoder behavior, PTT, CAT or flashing work changed.


## 2026-10-08 — JS8 WAV-to-tone reconstruction bridge

- Added host-only `tools/js8-wav-tones.cpp`: strict 12 kHz mono i16 WAV -> Normal spectral search -> candidate tone demod -> machine-readable 79-tone frame lines.
- Its output can be redirected directly into `js8-tone-map`; metadata is emitted as comment lines so no manual frame transcription is required.
- Sandbox validation on a generated noisy fixture recovered one candidate at the injected 0.160 s / 900.000 Hz position with 21/21 sync hits and an exact match to the documented 79-tone API vector.
- The tool uses the correctness-oracle spectral path and supports narrowing the audio-frequency span; its runtime is not a P4 performance claim.
- No firmware binding, UI, shared FT8/FT4 decoder behavior, PTT, CAT or flashing work changed.


## 2026-10-08 — JS8 tone frames to codeword export

- Added `js8-map-codewords`: verified Normal 79-tone frames plus an explicit validated tone-label permutation -> 174-bit channel-word lines for `js8-fec-reconstruct`.
- Added reusable tone-label parsing and negative tests for duplicate/incomplete mappings; optimized and ASan/UBSan tone-map tests pass in the sandbox.
- Verified the host reconstruction plumbing on the synthetic Normal fixture: WAV extraction -> 79-tone frame -> explicit mapping -> one 174-bit codeword.
- No real JS8 tone mapping or FEC result is claimed from the plumbing fixture.
- No firmware binding, UI, shared FT8/FT4 decoder behavior, PTT, CAT or flashing work changed.


## 2026-10-08 — JS8 CRC reconstruction lab

- Added host-only `js8-crc-reconstruct.*` for the future 87-bit information-word corpus: it searches all non-zero 12-bit feedback masks, MSB/LSB recurrences and direct/reversed observed CRC-bit order while inferring the fixed-length affine offset.
- Added optimized and ASan/UBSan primitive tests plus a synthetic hidden-parameter corpus. The method recovers the intended test representation and its mathematically equivalent reflected form; that orientation ambiguity is explicitly documented.
- No JS8 CRC polynomial, initialization, xor-out or bit ordering is claimed yet; those remain reconstruction results to be established from FEC-decoded real/reference frames and held-out validation.
- No firmware binding, UI, shared FT8/FT4 decoder behavior, PTT, CAT or flashing work changed.


## 2026-10-08 — JS8 pre-FEC front-end pipeline

- Added standalone `js8_frontend.*`: bounded, allocation-free orchestration from caller-owned spectral grid through Normal sync search and candidate-local PCM demod to raw 79-tone candidate frames.
- Added an end-to-end synthetic front-half test that recovers the documented Normal frame, exact injected start sample, tone-0 frequency and all 21 sync tones; optimized and ASan/UBSan runs pass in the sandbox.
- The front end intentionally returns raw candidates only. It cannot create `Decode` or user-visible text until independently reconstructed FEC + CRC + frame parsing succeed.
- No firmware binding, UI, shared FT8/FT4 decoder behavior, PTT, CAT or flashing work changed.


## 2026-10-08 — JS8 sparse parity-check reconstruction

- Added host-only `js8-sparse-parity.hpp` and `js8-fec-sparse-search.cpp`: observed 174-bit codewords -> independent column signatures -> meet-in-the-middle weight-6 parity relations -> corpus validation and recovered-check rank.
- Added optimized and ASan/UBSan unit coverage. A 120-frame synthetic corpus with one hidden six-variable relation recovered exactly that relation and correctly reported that it did not span the entire synthetic parity space.
- Weight six is a bounded first search, not a claimed JS8 check weight. Real captures must establish the code rank/parity dimension and whether weight-6 relations are sufficient; incomplete rank is reported as incomplete.
- No firmware binding, UI, shared FT8/FT4 decoder behavior, PTT, CAT or flashing work changed.


## 2026-10-08 — JS8 graph-driven normalized-min-sum FEC engine

- Added standalone `js8_fec.*`: a graph-driven normalized-min-sum soft decoder with caller-supplied sparse adjacency and caller-owned workspace. It contains no guessed or copied JS8 parity graph.
- Added optimized and ASan/UBSan tests using a small synthetic repetition-chain code: clean early exit, correction of a weak wrong hard decision, graph/config/input validation, and undersized-workspace rejection.
- FEC convergence is explicitly parity-only and cannot create a JS8 decode without the later CRC + supported frame/plausibility gates.
- The real JS8 graph remains blocked on independent reconstruction from captured codewords; the decoder algorithm is ready to consume it once derived.
- No firmware binding, UI, shared FT8/FT4 decoder behavior, PTT, CAT or flashing work changed.


## 2026-10-08 — JS8 reconstructed graph generator

- Added host-only `js8-graph-builder.hpp` and `js8-fec-graph-gen.cpp` to convert recovered sparse parity checks into the immutable bidirectional adjacency arrays consumed by `js8_fec`.
- The generator refuses production output unless recovered sparse-check rank equals the independently measured parity-space dimension; an explicit `--allow-incomplete` override is host-experiment-only.
- Optimized and ASan/UBSan graph-builder tests pass. A deliberately incomplete synthetic reconstruction was correctly rejected at rank 1 of 54; the explicit override emitted the expected one-check graph.
- No real JS8 graph is present or claimed yet.
- No firmware binding, UI, shared FT8/FT4 decoder behavior, PTT, CAT or flashing work changed.


## 2026-10-08 — JS8 synthetic SNR estimator baseline

- Added standalone `js8_snr.*`: signal/noise estimation in the conventional 2500 Hz weak-signal bandwidth from an already-validated 79-tone frame and a caller-owned spectral grid. It is not a detector and must only be used after future FEC + CRC acceptance.
- Added `tools/js8-snr-sweep.cpp` and optimized/ASan/UBSan estimator tests. The deterministic plain-FSK/AWGN sweep recovered all 79 tones in 120/120 trials from +8 through -12 dB; mean estimator error at each step stayed within 0.05 dB. At -16 dB, 6/20 frames retained all 79 tones and those estimates averaged -0.03 dB error.
- Synthetic calibration offset stays 0.0 dB. This is not a real-JS8 calibration claim; reference-paired owner captures must validate it before firmware displays JS8 SNR as calibrated.
- No firmware binding, UI, shared FT8/FT4 decoder behavior, PTT, CAT or flashing work changed.
