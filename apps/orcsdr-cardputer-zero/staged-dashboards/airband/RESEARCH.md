# Sources, license and derivation

Research was scoped to correct VHF airband manual AM reception rather than importing Tab5 scanner/catalog functionality.

## Project sources inspected from `hardcoreerik/OrcSDR`

- `apps/orcsdr-tab5/ui/airband_scanner.hpp` and `airband_scanner.cpp`: 118.000–136.975 MHz band/guard and 25/8.33 spacing policies; this staged receiver intentionally supports **25 kHz only**. Its `ChannelSquelch` documents signal-relative threshold/floor behavior but is NOT copied into the staged code.
- `apps/orcsdr-tab5/ui/airband_audio_filter.cpp/.hpp`: 48 kHz AM voice 300–3,200 Hz filter reference; a small independent streaming filter is used here, not the file-global ESP/M5 implementation.
- `apps/orcsdr-tab5/ui/airband_runtime.cpp`, `airband_dashboard.hpp`: exposes ESP-IDF/NVS/PSRAM/UI dependencies, station catalog and scanning; none are copied into the Linux module.
- `apps/orcsdr-cardputer-zero/src/fm_session.cpp/.hpp`, `linux_audio.cpp/.hpp`, `rtl_receiver.hpp`, `iq_queue.hpp`: single RTL owner / bounded queue / generation metadata / ALSA exact-silence behavior. The staged module depends only on `IqMetadata` from `iq_queue.hpp`.
- `apps/orcsdr-cardputer-zero/src/compact/compact_renderer.hpp`: existing 5×7 glyph patterns adapted (same-project source) to avoid importing FM app UI into the dedicated Airband renderer. **Original project licensing preserved.**
- `core/portable/cardputer_radio/scan_dsp.h`: shared 1,024-bin FFT and waterfall behavior consulted; staged unit currently uses an isolated small 1,024-point in-folder FFT because the shared function lacks a selectable carrier-centered narrow zoom and metadata-specific plot update. A future coordinator-owned extraction can unify these if desirable.

Repository source: https://github.com/hardcoreerik/OrcSDR/tree/codex/cardputer-zero-ui-audio-controls
Repository license: https://github.com/hardcoreerik/OrcSDR/blob/codex/cardputer-zero-ui-audio-controls/LICENSE — **GNU AGPL-3.0**, including the adapted same-project font. No removed AGPL notices, third-party copied code, GPL/AGPL transplant or proprietary DSP dependency.

## External references read before DSP implementation

- [US Air Force Academy ECE447, Lab 3: Amplitude Modulation — Receiving Real Air Band Traffic](https://usafa-ece.github.io/ece447-book/labs/Lab3.html): educational reference on VHF full-carrier AM envelope demodulation, sidebands and removing the carrier/DC term. **Concept only; no source copied.**
- [EUROCONTROL 8.33 kHz implementation support](https://www.eurocontrol.int/function/833-khz-implementation-support): official description of aviation voice channel designators and the 6-digit identification convention. **Reason to omit channel-designator mode rather than fake it using `+8333` Hz.** No code/data copied.
- [UK Ofcom 8.33 kHz and frequency/channel-number guidance](https://www.ofcom.org.uk/siteassets/resources/documents/manage-your-licence/aeronautical/guidance/understanding-8.33khz-frequencies-and-their-specific-channel-number.pdf): source documenting that channel names need not equal actual carrier frequencies. Referenced for scope only, not copied.
- [Pluto-airband specification](https://github.com/juchong/pluto-airband/blob/master/SPEC.md): an independent overview of airband channel filtering, envelope DC removal, and voice processing. **Architectural comparison only**, no code derived/transplanted. License of its code was not verified here; no code used.

The NCO, modulo CIC, 80-to-48 kHz windowed-sinc polyphase filter, controller, test signals, preview, and incremental renderer were independently written in the stage folder. The Blackman window and sampled-sinc filter are standard mathematical DSP constructions. The small 5×7 glyph table is an adapted **within-repository** component and remains governed by OrcSDR's license.

## Explicit unverified claims

The CPU and memory measurements were performed on an x86_64 container, not the Cardputer Zero. No instrument-grade dBm calibration, USB RF continuity proof, aviation-grade 8.33 mapping, adjacent-channel conformance certificate, physical antenna measurement, or speaker/LCD acceptance is implied by the synthetic tests.
