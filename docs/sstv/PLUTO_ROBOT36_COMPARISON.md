# Pluto Robot36 WAV vs MMSSTV — observed decoder improvement

**Date:** 2026-10-12
**Branch:** `codex/sstv-native-dashboard`
**Reproduction recording:** 48 kHz / 16-bit mono PCM, 36.91 seconds, received through Pluto -> SDR audio chain.
**WAV SHA-256:** `546906c0c68f5798bcfde405d1db210de3500513ee45c4202c207496fcdaff33`

## Problem

MMSSTV decoded the same received Robot36 recording with substantially more readable lettering, fine black-white grid detail and resolution wedges than our SSTV receiver's zero-crossing-only luminance path. OrcSDR already detected VIS=8 and 240 sync events without overrun or missed sync; it was not an RF reception or line-lock failure.

The 12 kHz audio stage observes only ~3.3 samples per Robot36 picture pixel (88 ms/320 pixels). The original code updated its tone estimator only on rising zero crossings (5–10 samples per tone period), with an additional 0.25 IIR smoothing stage. This lost fast frequency transitions. The user provided an independent MMSSTV screenshot as an external behavioral reference; no MMSSTV executable was decompiled.

## Fix (source-driven, independent of MMSSTV)

- Retain the original zero-crossing frequency estimate for **VIS, parity, and line synchronization**, where it already proved reliable.
- Add a separate sample-rate `P=y[n-1]*(y[n]+y[n-2])`, `Q=y[n-1]^2` normalized autocorrelation estimator for luminance.
- Smooth `P,Q` independently with alpha `0.6`, then estimate `f=Fs/(2pi)*acos(P/(2Q))`.
- Avoid expensive `acos` for each 12 kHz sample using a 513-entry (`1,026 bytes`) frequency LUT built once in `start()`. Interpolate between neighboring entries.
- If estimated signal power is below the gate, use the existing tone estimate. Do not change the audio producer ring buffer or image storage.
- F4HWN's Apache-2.0-licensed SSTV model independently documents the P/Q signal identity: https://github.com/armel/uv-k1-k5v3-firmware-custom/tree/main/App/apps/sstv ; our implementation is independently structured and tuned using the user's measured audio.

## Observed results (exact same WAV)

| Metric | Prior branch receiver | Updated branch receiver |
| --- | ---: | ---: |
| VIS | 8 | 8 |
| Mode | ROBOT 36 | ROBOT 36 |
| Completed rows (with appended silence tail) | 240/240 | 240/240 |
| Sync locked | 240 | 240 |
| Sync missed | 0 | 0 |
| PCM queue overruns | 0 | 0 |
| Mean absolute horizontal gray difference (0–255 scale) | 4.10 | 14.97 |
| Raster visual detail | severe horizontal smearing | substantially clearer title, circles, grids and line wedges |

The grayscale horizontal gradient is a rough sharpness indicator, **not** a distortion, SNR, or SSIM measurement. No exact original Pluto-transmitted PNG was confirmed, so a calibrated pixel-error metric remains pending.

The host replay starts a 240-line image but the 36.91-second WAV ends immediately before the 240th line is committed. **Adding 4–5 ms of offline audio tail** finalizes the last line; this does not imply live RF reception necessarily drops the last row, because audio continues in live reception. This fix does **not** silently pad truncated captures.

**Update:** the host replay now calls `end_of_stream()`, which closes a final line only when its luminance scan has fully arrived. This recording therefore yields a complete 240-row, VIS-identified image (240 sync locked, 0 missed, 0 overruns) with no padding. The fixture's own capture metadata (`capture.json`, `audio-diagnosis.json`) records `rf_decode_verified: false` and `external_decoder_verified: false`; the MMSSTV comparison above remains an informal visual reference.

An independent deliberately difficult 1 ms alternating 1500/2300 Hz stripe on Robot36 row 100 (88 periods, exactly 88 ms) measured pixel-row edge energy (0–63 grayscale) of approximately **982 before vs 5,144 afterward**. The host smoke test now rejects edge energy <=2,500; it keeps VIS, sync and frame checks for Robot36, PD120, Scottie S1.

## Reproduce

1. Checkout `codex/sstv-native-dashboard` and fetch its latest GitHub Actions host artifact (`sstv-decoder-analysis-bundle`) or compile:

   ```sh
   g++ -std=c++17 -O2 -Iapps/orcsdr-tab5/ui \
      apps/orcsdr-tab5/tests/sstv_wav_replay.cpp \
      apps/orcsdr-tab5/ui/sstv_receiver.cpp \
      apps/orcsdr-tab5/ui/sstv_modes.cpp apps/orcsdr-tab5/ui/sstv_auto_detect.cpp -o sstv-wav-replay
   ```

2. Play the 48 kHz mono PCM WAV through the real host decoder:

   ```sh
   ./sstv-wav-replay /path/to/received-filtered-aligned.wav 8 239
   ```

   The executable logs VIS, mode, rows, sync locks/misses, queue overruns, and completed-frame count.

3. For testing line finalization (not production automatic behavior), append short silence to a **copy** of the WAV, then replay it. The original capture remains unchanged.

4. Run the branch host CI and full Tab5 build before evaluating firmware. Confirm firmware CPU timings and real-time radio receive independently. Host x86 optimization and image reconstruction are **not** ESP32-P4 benchmarks.

## Remaining work

- A verified native PD120 or Scottie capture using the new luminance estimator.
- True grayscale/pixel error against the **exact source PNG** transmitted by Pluto, not a screenshot-derived approximation.
- Analyze pixel-clock correction and phase slant on noisy/off-frequency WAV captures.
- Benchmark ESP32-P4 CPU utilization and heap from actual firmware.
- Full Robot36 color reconstruction (Y plus chroma) remains out of scope for this monochrome-first improvement.
