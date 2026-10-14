# OrcSDR SSTV — native progressive receiver (EXPERIMENTAL)

**Branch:** `codex/sstv-native-dashboard` (development continues on `claude/sstv-auto-dashboard`)  
**Target:** M5Stack Tab5, ESP32-P4, RTL-SDR (Blog V3C/V4/V4L, Nooelec NESDR SMArt V5)  
**Audio path:** receiver PCM → bounded audio tap → VIS/sync decoder → RGB565 PSRAM image → native M5GFX UI → background SD archive.

The receiver decodes **full colour** in all 14 modes: Robot 36/72 (Y with R-Y/B-Y chroma, interlaced in Robot 36), Martin and Scottie (G/B/R) and PD (Y, R-Y, B-Y, Y with chroma shared by a line pair). Host regressions send the colour ORC test pattern through an independent encoder in every mode (correlation 0.89-1.00, flat colours exact). **Hardware status:** a real over-the-air Robot 36 colour frame (Pluto, cabled, 145.500 MHz, RTL-SDR Blog V4L) was decoded on a Tab5 and saved to the SD card (correlation with the source R 0.90 / G 0.92 / B 0.87, at roughly 10 dB over the noise). A second frame, with the chroma cleanup below, reached R 0.915 / G 0.919 / B 0.905. Every other mode is verified on the host only; see *Not yet validated*.

## What the user sees

Open SSTV and the receiver listens by itself:

1. A valid VIS header names the mode at once (`ROBOT 36 - VIS CONFIRMED`).
2. Without a header, repeated 1200 Hz line-sync pulses are classified. After enough evidence one mode is chosen and the image starts at the next full line (`PD 120 - SYNC LIKELY - PARTIAL`).
3. Rows appear progressively. Complete and interrupted images go to a four-entry RAM gallery and, if auto-save is on, to the SD card.
4. The receiver returns to listening on its own, without restarting the RTL-SDR stream, so a following transmission in a different mode needs no action.

There is never more than one image decoder; the 14 modes are not decoded in parallel.

## How the mode is identified

**VIS (authoritative).** A 300 ms / 10 ms / 300 ms leader, a 30 ms start bit and eight 30 ms symbols (seven data bits, even parity, stop bit). Every symbol is a majority vote over its middle 20 ms; ambiguous symbols, a parity error or an unsupported code reject the header. A new valid VIS **always** wins: it archives any image in progress as a partial entry (`EndReason::new_vis`, including a speculative sync-identified one) and starts the new mode.

**Sync recovery (provisional).** Only when no VIS is being received and the strategy is *Auto*. The classifier keeps the last eight 1200 Hz pulse start times and widths and compares them with each mode's line period (±2.5 %) and sync width (±30 %). It locks only with ≥6 coherent pulses, a quality score ≥75 and a best-versus-runner-up margin ≥12. The score is a **relative heuristic, not a probability**. A sync-identified image:

- is always flagged `partial` and `start_unknown`: the top rows are unknown and nothing is invented;
- is shown as `PROVISIONAL LOCK` until eight further line syncs confirm it, and as `SYNC LOCKED - PARTIAL` afterwards. It is **never** shown as `VIS CONFIRMED`;
- is dropped back to listening after four consecutive predicted lines without a sync pulse (`EndReason::sync_lost`), keeping the rows already decoded.

Known weakness: a Robot 36 stream with every other pulse missing looks like Robot 72 to the classifier (same width, double period). A faded Robot 36 reception can therefore be mislabelled until the next VIS or until sync is lost.

**Manual (a VIS filter, not a forced decode).** *Manual* starts an image only when a valid VIS header names the chosen mode. Decoding a chosen mode without any VIS header is not implemented; the UI says so.

## Session state

`session_state()` derives one state from the decoder's own variables (no second copy): `searching`, `vis_acquisition`, `sync_analysis`, `provisional_lock`, `receiving`, `loss_recovery`, `finalizing` (first 0.5 s after an image ends), `ready` (until 3 s) and back to `searching`.

## Modules

| File | Role |
| --- | --- |
| `ui/sstv_receiver.*` | decoder, session state, gallery, reference-counted frames, metrics |
| `ui/sstv_modes.cpp` | 14-mode timing and VIS tables |
| `ui/sstv_auto_detect.*` | sync-only classifier and ranked candidates |
| `ui/sstv_status.*` | status wording (testable without a display) |
| `ui/sstv_settings.*` | detection strategy, manual mode, auto-save, persistence |
| `ui/sstv_archive.*` | background SD writer, schema v2 index, recovery |
| `ui/sstv_app.*` | dashboard lifecycle, user actions, archive hand-off |
| `ui/sstv_dashboard.*` | the five native screens (drawing and hit-testing only) |
| `ui/main.cpp` | hook adapters (RTL tuning, SD mount, clock, preferences) and one-line calls only |

## Real-time design

- Producer: 48 kHz PCM from the receiver, averaged ×4 into a 12 kHz, 4096-sample SPSC ring (8 KiB). The producer never draws, allocates or blocks.
- Decoder (UI task): `service()` handles a bounded number of samples per call. Workspace is two 800-byte luminance rows and a 513-entry (1 KiB) frequency table. Luminance uses a normalised autocorrelation estimator (alpha 0.60); VIS and sync keep the zero-crossing estimator.
- **64-bit sample clock.** A 32-bit counter at 12 kHz wraps after about 99.4 h, which would stall an unattended receiver; the raster origin is a double. Edge and pulse arithmetic stays wrap-safe.
- **Dropped audio** (ring full) is recorded in-band and replaced by the same amount of elapsed time, so the next line sync still locks. The tone estimators and VIS state restart across the gap.
- **Silence.** A fast envelope zeroes the tone estimate in true silence instead of holding the last tone.
- Frames (up to PD290 ≈ 963 KiB) are PSRAM, **reference counted**: the gallery, the live image and a pending SD write each hold a reference, so eviction can never free pixels under the writer.
- Metrics: `service_us_*`, `service_cpu_pct`, ring high-water mark, overruns, gaps, PSRAM frames. The host numbers printed by the tests are **not** P4 numbers.

## Dashboard

Five native screens (no WebView):

- **RECEIVE** — headline, mode and native size, VIS code or sync identification, image state (in progress, joined in progress with start row unknown), row progress, per-image sync locked/missed and line error, tone and level, abort and capture buttons.
- **IMAGES** — gallery tiles with mode, complete/partial, VIS/sync identification, *joined* marker and SD save status; detail panel with frequency, UTC (or "time not verified"), counters and file name.
- **SIGNAL** — tone monitor; sync pulse width and line interval; top-3 sync candidates with relative scores; missed lines, overruns and gaps; decoder cost, ring high-water, PSRAM; SD write latency.
- **TUNE** - replaces the old preset list. Top to bottom: a VFO bar (big frequency readout: tap it or ENTER FREQUENCY for the keypad; -/+ step the dial; STEP chip cycles 10 Hz to 25 kHz; MODE chip cycles AUTO, NFM, USB, LSB; GAIN chip toggles automatic and manual tuner gain, G-/G+ step the tuner's gain table), the **RF spectrum and waterfall** (240 kS/s, +-120 kHz, the tuned frequency in the middle; **tap anywhere to tune there**), the **audio sync comb** (what the decoder hears, 700-2700 Hz, with guide rails at 1200 sync, 1500 black, 1900 mid and 2300 white; the rails and frame are tinted by the receiver state, and a white tick marks the measured sync tone), and the preset chips. Frequencies typed as `145.5` are MHz, five or more digits (`14230`) are kHz.
  - **Tuning error.** In USB or LSB the audio is the offset from the dial, so the 1200 Hz sync tone reads high or low when the dial is off. The readout shows the measured tone (average of recent sync pulses, settled part, extrapolated) and APPLY CORRECTION moves the dial by the right amount in the right direction (USB: raise when the tone reads high; LSB: the opposite). In NFM the tone does not depend on the dial, so centre the signal on the spectrum instead.
  - AUTO mode picks LSB below 10 MHz, USB from 10 to 30 MHz and NFM above. The choices (frequency, step, mode, rails) are remembered.
  - **OrcDial.** The Dial has an SSTV dashboard (wire id 18): the knob tunes by the step shown, a centre press switches focus between frequency and step, and the keypad enters any frequency from 0.1 MHz to 1.7 GHz; a mode request cycles AUTO/NFM/USB/LSB. Re-flash the Dial for the new carousel entry.
- **SETUP** — *Auto (VIS + sync recovery)* (default), *VIS only*, *Manual (VIS filter)* with the 14 modes, auto-save, SD status. Changing settings never restarts the receiver or discards an image.

## Web export (read-only)

The Tab5's web console (port 80, when the web console is enabled) serves the newest finished SSTV picture to anything that can issue
an HTTP GET, such as OrcPixels. Both routes are **GET only**, need no authentication, and send `Cache-Control: no-store`. They serve the
newest picture in the RAM gallery (the one `SSTV LAST` reports): they never read the SD card, never serve a picture still being
received, and never change the radio, the tuning or the SD card. A snapshot is published by the SSTV task when the newest
picture, or its archive file name, changes; the web server only reads that snapshot. The last published picture stays available after
the SSTV dashboard closes and is replaced only by a newer one, cleared by CLEAR RAM GALLERY, or lost at reboot.

- `GET /api/sstv`: always `200`, `application/json`. An empty gallery gives `{"none":true}`. Otherwise `{"none":false,"serial","session_id",
  "frequency_hz","utc_epoch"` (0 when the clock was not verified)`,"mode","width","height","rows","partial","start_unknown","vis_code",
  "sync_locked","sync_missed","identification","end","filename"` (the SD name once the save has finished, otherwise an empty string)`,
  "preview_width":64,"preview_height"}`. The wording of `identification` and `end` is the same as `SSTV LAST`.
- `GET /api/sstv/preview`: `200` `image/bmp`, a 64-pixel-wide 24-bit bottom-up BMP (Robot 36: 64x48, at most 64x64, always under
  13 KB) of the decoded rows only; or `404` with the body `none` when the gallery is empty.

## SD archive

Saving never happens on the UI task, in the PCM callback or in the USB path. `sstv_archive` owns a three-slot queue and a low-priority worker (FreeRTOS task, 6 KiB stack, priority idle+1).

**Write speed (2026-10-14).** Files opened for writing get a 32 KiB cache-aligned PSRAM stdio buffer and the 16 KiB chunk buffer is cache aligned, as in `docs/TAB5_SD_PERFORMANCE.md`. Before this the archive used unaligned PSRAM buffers, which sends the ESP-IDF SDMMC driver down its one-sector-at-a-time fallback: a 952 KB PD120 picture took 3.8 to 5.5 s to save (about 0.2 MiB/s) and one write needed a retry. After: 448 ms for the same picture and 302 ms for a 230 KB Robot 36 picture, no retries (device test with the synthetic self-test). Leaving the dashboard right after a picture finished stalled for up to 5.2 s before; six such legs afterwards took 0.14 to 0.30 s.

```text
/sd/orcsdr/sstv/
    index.csv
    rx_<utc-or-0>_<hz>_<session>[-n].bmp
```

- Each BMP is written row by row to `<name>.bmp.part` and renamed only when complete. If the card is pulled mid-write the `.part` is never exposed as an image; the retry overwrites it, and `recover()` removes any left over.
- **One reception event = one file and one row**, even when two images contain identical pixels. A 64-bit FNV-1a content hash is recorded so duplicates can be grouped later; file names are never used for that.
- Auto-save queues receptions that finish after it was enabled (no back-fill). Receptions with fewer than 8 rows are skipped as noise. The *Capture* button saves a snapshot of the image in progress (`end_reason=captured`).
- Missing card: bounded retries (2 × 10 s), then `failed(no card)`. Full card (free space < image + 64 KiB): `failed(card full)`. Queue full: the job is dropped and retried at the next poll while the image is still in the gallery.
- `recover()` (requested on the worker at every dashboard open) deletes stale `.part` files and appends a row, marked `recovered`, for any BMP the index does not mention.
- An index written by the first release (no header version) is **moved aside** to `index-v1-<ms>.csv`, never edited.

**Index schema v2** (header line is exact): `session_id, utc_epoch, utc_verified, frequency_hz, mode, vis_code, identification, id_score, width, height, decoded_rows, complete, start_unknown, sync_locked, sync_missed, end_reason, content_hash, filename`. `utc_verified=0` and `utc_epoch=0` mean the clock was not verified. `session_id` restarts at 1 on every boot; combine it with `utc_epoch` or the hash. `sync_locked/sync_missed` count that image only.

## Supported modes (colour)

| Mode | VIS | Frame | Transmitted line | Channels (host correlation with the source) |
| --- | ---: | --- | ---: | --- |
| Robot 36 | 8 | 320x240 | 150 ms | Y + interlaced R-Y/B-Y (0.95) - **also received over the air** |
| Robot 72 | 12 | 320x240 | 300 ms | Y, R-Y, B-Y (0.96) |
| Martin M1 | 44 | 320x256 | 446.446 ms | G, B, R (0.94) |
| Martin M2 | 40 | 320x256 | 226.798 ms | G, B, R (0.90) |
| Scottie S1 | 60 | 320x256 | 428.220 ms | G, B, R; sync mid-line (0.94) |
| Scottie S2 | 56 | 320x256 | 277.692 ms | G, B, R; sync mid-line (0.91) |
| Scottie DX | 76 | 320x256 | 1050.300 ms | G, B, R; sync mid-line (1.00) |
| PD50 | 93 | 320x256 | 388.160 ms | Y0, R-Y, B-Y, Y1 (0.92) |
| PD90 | 99 | 320x256 | 703.040 ms | Y0, R-Y, B-Y, Y1 (0.96) |
| PD120 | 95 | 640x496 | 508.480 ms | Y0, R-Y, B-Y, Y1; ISS priority (0.94) |
| PD160 | 98 | 512x400 | 804.416 ms | Y0, R-Y, B-Y, Y1 (0.96) |
| PD180 | 96 | 640x496 | 754.240 ms | Y0, R-Y, B-Y, Y1 (0.96) |
| PD240 | 97 | 640x496 | 1000.000 ms | Y0, R-Y, B-Y, Y1 (0.97) |
| PD290 | 94 | 800x616 | 937.280 ms | Y0, R-Y, B-Y, Y1 (0.97) |

Colour layout lives in one table (`build_layout` in `ui/sstv_receiver.cpp`); the chroma-to-RGB conversion is slowrx's YUV formula. Sync-pulse latency is calibrated **per mode** (`sync_latency`), by sweeping it on the host against the independent encoder: alignment is very sensitive to it (a 4-sample error costs about 0.1 correlation in Martin M2), and a real receiver adds its own small delay, so confirm each mode with a recording before trusting the figure.

The authoritative constants are in `ui/sstv_modes.cpp` (integer microseconds/nanoseconds, traced to slowrx `modespec.c`, the Rust slowrx port and the sstv.org.uk mode notes). `tools/sstv_validate_timings.py` audits them in CI.

## Build, flash and test (Windows)

**Firmware** (ESP-IDF 5.5.4, never PlatformIO):

```powershell
.\apps\orcsdr-tab5\tools\build-tab5-idf.ps1
```

Flash only the Tab5's own port (never auto-detect; other boards may be attached):

```powershell
. C:\Espressif\frameworks\esp-idf-v5.5.4\export.ps1
cd apps\orcsdr-tab5
idf.py -B build-native-hosted3 -p COM17 flash
```

**Host regressions** (Linux, or WSL on Windows — no host compiler is required on Windows itself):

```bash
mkdir -p logs && cd logs
SSTV_FIXTURE_WAV=/path/to/received-filtered-aligned.wav ../tools/sstv_host_tests.sh
```

The script builds each suite with `-Wall -Wextra`, runs it, and writes a timestamped build log and run log per suite plus `sstv-summary-<stamp>.log` (commit, compiler, flags, exit codes and, for the replay, the fixture path, size and SHA-256) **into the directory it was launched from**. Without `SSTV_FIXTURE_WAV` the replay step is skipped and reported as such.

| Suite | Covers |
| --- | --- |
| `sstv_auto_detect_test` | all 14 sync signatures, irregular noise, counter wrap, ranked candidates |
| `sstv_receiver_smoke` | Robot 36, PD120 progressive, Scottie S1, 1 ms stripe regression, missing/disabled sync, new VIS during a reception |
| `sstv_auto_state_test` | 32-bit clock rollover, dropped audio, silence, per-session counters, Robot 36 → PD120 → Scottie S1 hands-off, noise and false pulse trains, damaged VIS, mid-join, retune/abort/loss and recovery, fade, ±1 % clock drift, gallery ownership, pinned frames, state machine, snapshots, VIS pre-empting a sync lock |
| `sstv_archive_test` | atomic saves, v2 index, partial/sync rows, identical images, missing/full/pulled/slow card, bounded queue, eviction while pending, recovery, v1 migration, auto-save switch |
| `sstv_status_test` | wording rules, including *VIS CONFIRMED only for a VIS identification* |
| `sstv_settings_test` | persistence, sanitising, what each strategy does to the receiver |
| `sstv_noise_test` | white, clipped and filtered noise, DC, silence and hopping tones: no stall, bounded service time (real RF exposed this input class) |
| `sstv_colour_test` | colour ORC pattern in all 14 modes by an independent encoder (per-channel correlation and error), plus flat R/G/B/C/M/Y/white/black stripes in one mode of each layout |

The 36.91 s Pluto Robot 36 recording (`received-filtered-aligned.wav`, SHA-256 `546906c0c68f5798bcfde405d1db210de3500513ee45c4202c207496fcdaff33`, 48 kHz PCM16 mono, captured with `rtl_fm` at 145.5 MHz; its own metadata records `rf_decode_verified: false`) now decodes to a complete 240-row VIS-identified image with 240 locked and 0 missed syncs, using the explicit end-of-stream flush (`end_of_stream()`), not padded audio.

## Not yet validated

- **Observed on a Tab5 (2026-10-12):** dashboard rendering and touch, the Robot 36 and PD120 self-tests through the real decoder, display and SD archive, BMP saves of full-size pictures (after the 16 KiB block-write fix), and live reception of two over-the-air colour Robot 36 frames saved to the card.
- **Not yet observed:** long unattended runs and soaks beyond a handful of images, SD behaviour across many card types or a card pulled mid-write on real hardware (the failure cases are covered on the host only), coexistence with FM and USB reception, and P4 CPU/heap/PSRAM and latency figures beyond the spot readings in `SSTV STATUS`.
- Cabled or over-the-air reception of every mode except Robot 36; Martin, Scottie and PD are validated with synthetic audio only.
- Colour over the air for every mode except Robot 36; chroma smoothing for weak signals (colour speckle is visible at about 10 dB over the noise); Goertzel-based tone validation.
- Sync-only identification under fading is heuristic; see the Robot 36 / Robot 72 alias above. Martin's 4.9 ms sync pulse reads shorter than the classifier's floor, so Martin pictures are identified by VIS only.
- `session_id` restarts every boot; a paginated SD browser does not exist (the gallery shows the newest four).


## Chroma cleanup (weak signals)

Chroma is noisier than luma, and on a weak signal it shows as coloured speckle on gray areas. `clean_chroma` in `ui/sstv_receiver.cpp` applies a 1-2-1 smoothing across each chroma line, then removes chroma within 12 of neutral, ramping to full strength at 24. Measured on a real over-the-air frame (about 10 dB over the noise) it cut the mean saturation of the gray areas from 41 to 16 (source 19) and raised correlation from 0.90/0.92/0.87 to 0.92/0.92/0.91, while leaving the clean synthetic results unchanged (Robot 36 0.950 against 0.951; flat-colour stripes within 8 of 255). The cost is that a genuinely pale colour (chroma offset below about 24, for example the R-Y of yellow) is attenuated; a stronger floor cleans more but costs more. `kChromaTaps` and `kChromaCore` set it, and the host tests can override them with `SSTV_CHROMA_TAPS` / `SSTV_CHROMA_CORE`. An adaptive or user-selectable level is future work.

## Live RF: what it took (2026-10-12)

The on-device self-test injects audio directly into the decoder, which hid three faults that only real radio audio exposed:

1. The radio loop only demodulated at the default sample rate and while the speaker, web audio or recording wanted audio; the 240 kS/s rate SSTV uses was skipped, so `audio_offered` stayed 0. An active SSTV receiver now owns demodulation (`sstv_owns_audio` in `ui/main.cpp`).
2. At 240 kS/s a 32 KB USB block yields about 3277 audio samples into a 2048-sample buffer and hangs the device; custom-rate blocks are demodulated in 8 KB slices.
3. `SSTV STATUS` now reports `tone_hz`, `audio_level_milli`, `audio_offered` and `samples`: `audio_offered=0` means the radio path is not delivering audio at all.

`tools/sstv_host_tests.sh` replays recordings too: `SSTV_REPLAY_PPM=out.ppm` makes `sstv_wav_replay` write the decoded colour picture. A dongle comparison matters: the same cabled signal sat about 5 dB over the noise on a Nooelec and about 8-10 dB on an RTL-SDR Blog V4L, which was the difference between no decode and a clean one.

## Change record: TUNE tab (2026-10-13, branch claude/sstv-tune-tab)

- New TUNE tab replaces FREQUENCIES (see Dashboard). New pure modules `sstv_tune` (steps, tap-to-tune, keypad entry parsing, tuning error, palette) and `sstv_audio_scope` (512-point audio spectrogram), both host-tested (`tune` suite).
- Decoder: `Metrics.sync_tone_hz` / `sync_tone_pulses` (settled, extrapolated mean of the 1200 Hz sync pulse; expires after 5 s without pulses; `syncmeter` suite, including a 60 Hz mistuned picture that still decodes) and `set_audio_observer()` for the comb.
- Firmware: LSB/USB/NFM chosen from the tab (`effective_demod`), tuner gain auto/manual with a step table, RF spectrum shown on the tab through the existing spectrum pipeline (one gate in `main.cpp`), persisted frequency/step/mode/rails. `SSTV TUNE <hz>` on the serial console.
- OrcDial: SSTV dashboard, wire id 18 (carousel entry, direct keypad tune, knob tunes by the step shown). The Dial firmware must be re-flashed to show the new entry; the Tab5 never needs a Dial.
- Not verified on hardware yet: the TUNE tab on the Tab5 screen, LSB/USB sync-tone correction against a real SSB signal, and the OrcDial entry. Host-only so far.
