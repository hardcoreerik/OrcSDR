# OrcSDR SSTV — native progressive receiver (EXPERIMENTAL)

**Branch:** `codex/sstv-native-dashboard`  
**Target:** M5Stack Tab5, ESP32-P4, RTL-SDR V4/compatible device  
**Architecture:** real receiver PCM -> bounded audio tap -> VIS/sync decoder -> RGB565 PSRAM image -> native M5GFX UI -> explicit or optional automatic SD capture.

This receiver currently renders **monochrome** from valid luminance scans in all 14 modes. It is **not** a verified full-color receiver. Color-difference demodulation, exact Scottie edge validation, noise robustness and actual Tab5 firmware validation remain before a production release.

## Why this design

The F4HWN/armel UV-K1 / UV-K5 V3 Labs overlay demonstrates that scanline SSTV is possible with very limited scratch RAM. OrcSDR follows that architectural lesson without copying its code: **no complete PCM frame, no temporary image stack, no display calls inside the DSP producer**.

UI pages: **Receive**, **Images**, **Signal**, **Frequencies**, **Setup**. The Receive page paints resampled screen scanlines only after the decoder publishes completed luminance rows; images appear before the frame completes. On 640×496 PD120 the UI retains native source resolution while fitting the preview to the 1280×720 screen.

Mode detection: validated even-parity VIS preamble (1900 / 1200 / 1900 / 1200 start), then table dispatch. The parser rejects unsupported VIS and parity/sync errors.

Raster: sample clocks are double precision in the *UI receiver* and never allocated per line. The signal path uses a one-pole highpass + lowpass and fractional zero-crossing tone estimator. A gated sync pulse detector nudges line origin by 25% of measured offset, avoiding a new frame buffer for slant correction. Its robustness under weak RF remains unproven.

Workspace: 2×800 8-bit luminance pixels (1600 bytes) plus scalar timing state. The producer-to-consumer audio queue is a separate 4096×16-bit (8 KB) transport buffer with overrun counting. Only the final source image plus a bounded gallery use PSRAM.

## Supported monochrome preview mode table

| Mode | VIS decimal | Frame | Transmitted line | Notes |
| --- | ---: | --- | ---: | --- |
| Robot 36 | 8 | 320×240 | 150 ms | Y scan |
| Robot 72 | 12 | 320×240 | 300 ms | Y scan |
| Martin M1 | 44 | 320×256 | 446.446 ms | G used as monochrome |
| Martin M2 | 40 | 320×256 | 226.799 ms | G used as monochrome |
| Scottie S1 | 60 | 320×256 | 428.380 ms | G used, sync mid-line |
| Scottie S2 | 56 | 320×256 | 277.692 ms | G used, sync mid-line |
| Scottie DX | 76 | 320×256 | 1050.300 ms | G used, sync mid-line |
| PD50 | 93 | 320×256 | 388.160 ms | Y0/Y1 pair |
| PD90 | 99 | 320×256 | 703.040 ms | Y0/Y1 pair |
| PD120 | 95 | 640×496 | 508.480 ms | Y0/Y1 pair, ISS priority |
| PD160 | 98 | 512×400 | 804.416 ms | Y0/Y1 pair |
| PD180 | 96 | 640×496 | 754.240 ms | Y0/Y1 pair |
| PD240 | 97 | 640×496 | 1000.000 ms | Y0/Y1 pair |
| PD290 | 94 | 800×616 | 937.280 ms | Y0/Y1 pair |

The **authoritative implementation constants** are in `apps/orcsdr-tab5/ui/sstv_modes.cpp` in integer microseconds/nanoseconds. Each is traceable to **slowrx modespec.c**, the [Rust slowrx port](https://docs.rs/slowrx/latest/src/slowrx/modespec.rs.html), the [SSTV mode family notes](https://sstv.org.uk/docs/modes/), and the [PD timings](https://sstv.org.uk/docs/modes/pd/). Slowrx is ISC-licensed; mode timing facts and the algorithm have been independently expressed here, without copying decoder code.

## Capture an SSTV image: on-device receive database

The **Capture Image** button stores the current progress if at least one line has arrived; otherwise it captures the latest completed image. Auto-save (Setup) stores completed images. A reception is **partial** if interrupted and captured before its final line.

SD layout:

```text
/orcsdr/sstv/
    index.csv
    rx_<utc-or-zero>_<hz>_<session-id>.bmp
```

- CSV columns: `session_id,utc_epoch,frequency_hz,mode,width,height,decoded_rows,partial,sync_missed,filename`.
- `utc_epoch = 0` explicitly means the receiver clock was not verified.
- `partial = 1` records incomplete reception. Unreceived lines are black (no invented samples).
- The BMP is first written to a `.part` temporary file, then renamed, then the CSV is appended. An interrupted write may leave a `.part` file. If index append fails after rename, a BMP may remain, recoverable by directory scan.
- File paths are checked to avoid overwriting captures after a reboot.
- The current **Images** dashboard browses the four-entry RAM gallery. A paginated, indexed SD-gallery browser is not yet built. Export the CSV/BMP files for long-term search and comparison.

**Performance warning:** SD writes currently happen on the UI task, not in the 12 kHz decoder or USB callback. A slow card can stall display and allow the small audio queue to overrun during a new image. Before shipping unattended auto-capture, introduce a low-priority storage worker that owns a stable completed-image snapshot and never blocks the decoder.

## Build, run, and test

1. Check out `codex/sstv-native-dashboard`. Follow `ENVIRONMENT.md` for the approved native **ESP-IDF 5.5.4** setup; do not use the old PlatformIO build. Build with `tools/build-tab5-idf.ps1` on the qualified workstation.
2. Run the algorithm smoke test on a Linux desktop:

   ```sh
   g++ -std=c++17 -O2 -Wall -Wextra -Iapps/orcsdr-tab5/ui \
       apps/orcsdr-tab5/tests/sstv_receiver_smoke.cpp \
       apps/orcsdr-tab5/ui/sstv_receiver.cpp \
       apps/orcsdr-tab5/ui/sstv_modes.cpp -o /tmp/sstv-smoke
   /tmp/sstv-smoke
   ```

   This test synthesizes Robot36 and PD120 audio, confirms VIS decoding, and checks **PD120 rows are visible before completion**.

3. Compile the real-WAV replay harness (48 kHz, PCM16, mono, **demodulated** SSTV audio):

   ```sh
   g++ -std=c++17 -O2 -Wall -Wextra -Iapps/orcsdr-tab5/ui \
       apps/orcsdr-tab5/tests/sstv_wav_replay.cpp \
       apps/orcsdr-tab5/ui/sstv_receiver.cpp \
       apps/orcsdr-tab5/ui/sstv_modes.cpp -o /tmp/sstv-wav-replay
   /tmp/sstv-wav-replay /path/to/received-pd120.wav 95 100
   ```

   Arguments `95 100` mean expected VIS 95, at least 100 decoded rows. Use original lossless WAVs where possible; do not substitute an MP3 for signal-performance qualification.

4. Flash the Tab5, choose **SSTV**, select **ISS SSTV** (145.800 MHz FM) only during an announced ISS transmission, or choose **HF SSTV** (14.230 MHz USB). Confirm actual RSSI/PCM and validate with known audio/over-the-air captures.
5. Check **VIS OK / ERR**, **sync locked/missed**, raster progress, BMP file and CSV index. Repeat with missing SD, noisy audio, missing VIS, late join, interrupted image, no RTL-SDR, retune, manual abort, and alternate modes. Capture serial console logs and fixture file SHA256 in the validation report.

Test harnesses can be run in the repository checkout folder with redirection such as `/tmp/sstv-smoke > sstv-test.log 2>&1` to persist logs in the directory where the test is launched.

## Feature priorities informed by actual community experience

- **PD120 / ISS reception and auto VIS:** [ISS reception discussions](https://www.reddit.com/r/amateursatellites/comments/1wvucfc/iss_sstv/).
- **Live line-by-line painting:** explicitly praised in the [slowscan.space decoder discussion](https://www.reddit.com/r/amateursatellites/comments/1w1myl0/my_sstv_project_got_a_bit_out_of_hand_here_is_a/).
- **Replay and corrections:** users exchange WAVs and use separate decoders; see [ISS recording difficulties](https://www.reddit.com/r/amateursatellites/comments/1hpli4u) and [practical SSTV applications](https://github.com/bucknova/Open-SSTV/blob/main/SSTV_App_User_Guide.md).
- **Capture history:** add a persistent thumbnail index and filters by date, mode, source frequency, partial/complete, and reception quality. Future host-side perceptual hashing can group repeated transmission art while preserving separate reception logs.

These are qualitative, public community signals, not a statistically representative survey or ranking of usage.

## Unresolved

- No verified **ESP-IDF Tab5 firmware build** or live RTL-SDR RF hardware validation.
- Scottie line-start, all 14 modes, and real noise/slant tolerance need recorded-fixture regression; current automatic smoke validates Robot36 and PD120 only.
- Full RGB/YUV reconstruction, PD chroma, small Goertzel-based mode/sync tone validation, background SD task, archived gallery reindex, and audio/IQ replay are future work.
- The current tone front-end is a lightweight band-limited crossing estimator, **not a Goertzel implementation**. Frequency accuracy at near-black tones should be validated against Goertzel and/or quadrature demodulation before production.
