# Targeted source review and provenance

No new third-party code, datasets, external network services, map tiles or captures were imported into this module. The interface and waveform tests derive from OrcSDR's existing in-repository native code; their licenses/notices remain with the original files.

Primary sources examined on the **integration branch** `codex/cardputer-zero-ui-audio-controls` at baseline `73736f9e784dd8fcc20c935620740a00dcbca301`:

- [Portable native ADS-B decoder](https://github.com/hardcoreerik/OrcSDR/blob/codex/cardputer-zero-ui-audio-controls/apps/orcsdr-tab5/ui/adsb_decoder.cpp): CRC gate, DF17 identification/position/velocity, same-ICAO CPR helper, 2.048 MS/s pulse interpolation and 16,640-sample internal magnitude capacity. The staged CMake compiles this original source; the dashboard does **not** vendor/copy it.
- [Decoder data structures](https://github.com/hardcoreerik/OrcSDR/blob/codex/cardputer-zero-ui-audio-controls/apps/orcsdr-tab5/ui/adsb_decoder.hpp): `Frame`, `Stats`, callbacks.
- [Original Tab5 dashboard](https://github.com/hardcoreerik/OrcSDR/blob/codex/cardputer-zero-ui-audio-controls/apps/orcsdr-tab5/ui/adsb_dashboard.hpp): field meanings and live UI behavior, **not** its display/map plumbing.
- [Linux live pipeline](https://github.com/hardcoreerik/OrcSDR/blob/codex/cardputer-zero-ui-audio-controls/apps/orcsdr-cardputer-zero/src/live_capture.cpp): strict `2048000` ADS-B rate validation, synchronous worker decoder path, bounded SDR CLI, known application drops vs unknowable upstream USB loss.
- [Linux `IqMetadata`](https://github.com/hardcoreerik/OrcSDR/blob/codex/cardputer-zero-ui-audio-controls/apps/orcsdr-cardputer-zero/src/iq_queue.hpp): first-sample index, monotonic timestamp, queue discontinuity, generation.
- [Existing portable renderer](https://github.com/hardcoreerik/OrcSDR/blob/codex/cardputer-zero-ui-audio-controls/apps/orcsdr-cardputer-zero/src/compact/compact_renderer.hpp): Cardputer screen dimensions, RGB565 colors and 5×7 pixel font approach. The staged dashboard deliberately uses a small local drawing primitive to avoid importing FM view layout and controls.

The known CRC-valid identification / CPR sample bytes and the deterministic CU8 pulse construction were taken from the existing decoder's **self-check**. The staged tests generate those samples in memory; no large binary fixture or screenshot is committed. Code ownership and original project copyright/license requirements are unchanged. No external license attribution is necessary for independent orchestration or the generic display glyphs reproduced from the OrcSDR renderer.

**Rate mismatch:** native interpolation uses bit centers separated by 2048 milli-samples, and the 246-sample preamble window is explicitly written for 2.048 MS/s. A deterministic 2.56 MS/s synthetic pulse shows no CRC-valid result in the fixed-rate decoder. The first module therefore refuses rate mismatches rather than adding an unchecked resampler.
