# Machine Learning / Signal Intelligence (experimental)

Branch: `codex/machine-learning`
Status: **Phase 1 implemented in source and host-tested; ESP32-P4 firmware build and on-device validation pending.**

## Product goal

Give the Tab5 Smart VFO Home an **optional**, non-invasive signal activity
overlay today and a trustworthy, replaceable on-device classifier later.
The UI must not infer a protocol from a band plan or a spectral peak.

This work draws from the separate, private OrcNode evidence-detector research:
`hardcoreerik/orcnode` `docs/SCANNER_ARCHITECTURE.md`,
`docs/FEATURES18.md`, and `docs/ON_DEVICE_ML_PHASES.md`.
OrcNode has FFT scanning, K-of-N detection, 18 host-matched IQ features,
and a separately bench-tested ESP-DL sample runtime; **no trained RF model
is validated on the P4**. Those concepts are adapted here, not copied as
an entire multi-receiver scan engine. No OrcNode dependency is added to the
OrcSDR firmware or `esp-rtl-sdr` driver.

## Phase 1: currently implemented

- Opt-in `SIG ID: OFF / ON` chip above the Home spectrum; toggle by touch.
  Default OFF. Setting is **session-only** and is not saved to NVS yet.
- Reuse the Home spectrum's **existing** resampled dB-per-pixel data after
  the display smoothing step. There is no second IQ consumer, FFT, USB
  callback, polling task, model, or new SDR driver API.
- Quantile noise estimate, 9 dB detection threshold, 5 dB region-edge
  threshold, and 3 hits in 8 display updates before a marker appears.
  Bounded to 8 tracks. Tracking is reset on retune, zoom, FFT window change,
  disabling the feature, or leaving Home.
- Markers are spectrum-frequency anchored. Tap-to-tune remains the normal
  Home control. Label `RF +N dB` is **relative spectral excess**, neither
  absolute power in dBm nor ML confidence. No modulation/protocol prediction
  is displayed.
- All feature calculations occur on the normal Home rendering cadence
  (approximately 10 fps, or roughly 3 fps under audio stress). No heap
  allocation or blocking I/O in the detector; one small stack array for
  quantile estimation; no high-rate IQ capture.
- Existing DSP operations and audio take priority. The overlay is
  display-only and never adjusts gain, RF mode, or tuning automatically.

## Testing

Host source is `apps/orcsdr-tab5/ui/signal_id.hpp/.cpp`; regression
suite is `tests/signal_id_tests.cpp` and `tools/test-signal-id.sh`.

From repository root, Linux/WSL:

```bash
bash tools/test-signal-id.sh
```

Run produces `signal-id-host-test.log` in the **working directory**.
Tests cover quiet baselines, single-frame spikes, a persistent signal,
independent simultaneous signals, peak-to-frequency conversion, retune
invalidation, stale expiry, non-finite data, and large absolute frequency.
The GitHub Actions host workflow runs the same suite on branch pushes/PRs.

For a hardware test, build the Tab5 through its **existing documented**
ESP-IDF 5.5.4 workflow; do **not** flash OrcNode's ESP-IDF 6.1
firmware or alter the embedded C6 image policy. With an RTL-SDR attached:

1. Start the Home waterfall and verify normal audio, USB, and waterfall FPS
   with signal identification OFF.
2. Tap `SIG ID: OFF`. Observe `SIG ID: ON` and a marker only after
   a real persistent energy peak crosses the local threshold.
3. Tap a detected signal in the spectrum: standard Home tap-to-tune should
   still work. Retune or zoom; old markers must disappear until new
   observations meet the persistence requirement.
4. Switch the overlay OFF and check that old markers clear at the next
   spectrum frame. Return to Home and repeat with different spans.
5. Record RF gain, signal conditions, CPU/DSP timing, waterfall FPS,
   `RTL_DSP STATS`, USB drops/overruns, audio underruns, and any false
   markers. Repeat with the display in its audio-stressed refresh mode.

**No on-device build or hardware trial is claimed by this branch yet.**
The core host regression alone does not verify the graphics integration.

## Known limitations

- The estimator sees only currently displayed bins; no off-screen scans.
- Display-smoothed, resampled spectrum is adequate for activity markers,
  **not** a substitute for raw IQ classification or calibrated RF power.
- Narrow DC-centered spurs, wideband raised floor, transients, adjacent
  signals, and peaks near the FFT edges can produce misses or misgrouping.
- Markers do not yet show time-history, tap-to-inspect metadata, persistence
  across reboots, logging, protocol confidence, or a manual "unknown" review.
- A dense spectrum is bounded to eight tracks; excess candidates are dropped
  without affecting USB/DSP. The K-of-N filter can add 0.2–1 seconds of
  visible-label latency depending on display cadence.
- The optional overlay may add measurable CPU and draw work; benchmark on
  actual Tab5 hardware before default-enabling.

## Phase 2: actual ML (not yet implemented)

1. Freeze a versioned, **raw IQ** training contract (CU8, receiver/rate/gain,
   band, window length, quality flags, class-map and capture hashes). Adapt
   OrcNode's `features18.v1` only after host-parity fixtures are portable.
2. Compare power-only / DSP rules, an 18-feature classical classifier, and a
   compact quantized IQ network against the **same held-out** data. Separate
   detection, modulation family, protocol decoding, and novel/unknown labels.
3. Use OrcAnomaly's repeatable labelled PlutoSDR emissions and genuine
   reception captures. Report per-class recall, precision, SNR slices,
   calibration, unknown rejection, and false positives. Do not promote
   synthetic-only results to hardware acceptance.
4. If a learned candidate wins, add a versioned `Inference` interface
   behind a bounded lower-priority task, compare ESP-DL/ESP-TFLite-Micro
   runtime cost and host-to-P4 parity. Skipped/late model invocations are
   surfaced as unavailable, **never silently relabeled as unknown**.
5. Move to proposed demodulation/filter/dashboard suggestions only after
   a validation record. A protocol is `confirmed` only after an actual
   decoder validates it. Auto retune remains OFF unless requested.

OrcNode validated ESP-DL **sample sine model** coexistence with three
independent 2.4 MS/s scanners, but that is not an RF model performance
measurement and cannot be extrapolated to the busy Tab5 audio/display path.
