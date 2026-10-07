# FT8 RX dashboard sandbox

Status: **sandbox baseline, RX only, decoder not yet bound**.

This branch establishes the FT8 product surface and the embedded-friendly data
contracts before a decoder implementation is selected and benchmarked on the
M5Stack Tab5 / ESP32-P4. It deliberately does **not** claim live FT8 decoding.

## UX

The dashboard follows OrcSDR's existing 1280x720 M5GFX language: black
background, cyan outlines, green active state, large touch targets, shared
header geometry, and six bottom tabs:

- **LIVE** — dial frequency, USB/passband context, UTC slot state, decoder
  state, candidate/last-slot counts, and the newest decoded rows.
- **DECODES** — session decode table with UTC, SNR, time offset, audio offset,
  message class, message text, and locator.
- **MAP** — fully offline Maidenhead world grid. Pins are derived from locators
  decoded from received messages. The UI explicitly describes them as
  station-reported locators rather than measured positions.
- **BANDS** — touch presets for conventional FT8 dial frequencies. Presets are
  receive shortcuts, not a claim about regulatory allocations.
- **HEARD** — unique callsigns derived only from decoded traffic. No Internet
  lookup or enrichment is required.
- **SETUP** — RX-only status, decoder binding, UTC readiness, passband, map
  source, and network requirement.

## Why the slot clock is first-class UI

FT8 uses 15-second T/R sequences and 8-tone GFSK at 6.25 baud in current
WSJT-X protocol documentation. OrcSDR therefore treats accurate UTC as part of
the decoder state rather than hiding clock quality in settings. When UTC is not
established, the UI says so instead of implying that a decode window is valid.

## DSP boundary

`ft8_decoder_backend.hpp` is intentionally tiny. The planned receive path is:

```text
RTL-SDR IQ
  -> dedicated FT8 channel selection / USB baseband
  -> 12 kHz mono PCM, roughly 200-3000 Hz useful audio
  -> DecoderBackend
  -> orcsdr::ft8::Decode[]
  -> dashboard / log / Maidenhead grid
```

Do not wire the FT8 decoder through the existing Shortwave speaker-audio path
by assumption. The current DSP audit documents limitations in the generic SSB
path. FT8 should receive its own measured channel/baseband path so decoder
performance can be validated independently from speaker audio.

The first decoder candidate to benchmark is `kgoba/ft8_lib`: it is small C,
uses a waterfall/candidate/LDPC pipeline appropriate to embedded work, and is
MIT licensed. This branch does not vendor it. The gate for binding any decoder
is a separate benchmark showing memory, decode latency, and DSP load on the
Tab5 with saved and live IQ.

## Truth / evidence rules

The UI must keep these states distinct:

1. **UNBOUND** — dashboard/model exists but there is no decoder.
2. **REPLAY** — known sample data is being fed through a decoder or UI test.
3. **LIVE** — live RTL-SDR samples are feeding the decoder.
4. **DECODED** — a message passed decoder integrity checks.
5. **HEARD / MAP** — derived from decoded message content, not an independent
   identity or position measurement.

No demo decode rows are shown on the production LIVE screen when the decoder is
unbound.

## Baseline files

- `apps/orcsdr-tab5/ui/ft8_model.*` — band presets, slot math, message
  classification, Maidenhead parsing, bounded decode history helpers.
- `apps/orcsdr-tab5/ui/ft8_decoder_backend.hpp` — decoder seam.
- `apps/orcsdr-tab5/ui/ft8_dashboard.*` — six-tab M5GFX UI.
- `tests/ft8_model_tests.cpp` — host tests for slot timing, locator math, CQ
  parsing, and history stats.

## Next hardware gate

Before production integration:

1. Capture known-good live FT8 IQ on 20 m with the same RTL-SDR hardware used by
   OrcSDR.
2. Build a deterministic IQ -> 12 kHz FT8 channel path outside `main.cpp`.
3. Replay the same capture through the candidate decoder on host and Tab5.
4. Measure decoder heap/PSRAM use, worst-slot decode time, DSP-core load, and IQ
   drops.
5. Compare decoded messages against a known reference decoder for the same
   capture.
6. Only then wire the backend into the LIVE state and register the dashboard in
   Home navigation.

## Research notes

- WSJT-X User Guide 2.7.0 documents conventional working-frequency tables and
  notes that conventions can change or be user-edited.
- WSJT-X protocol documentation describes FT8 as LDPC(174,91), 8-GFSK at
  6.25 baud, 79 channel symbols, and about 50 Hz occupied bandwidth.
- `kgoba/ft8_lib` is MIT licensed and provides candidate search and FT8 decode
  APIs suitable for a later benchmark. No code from it is copied in this
  baseline.
