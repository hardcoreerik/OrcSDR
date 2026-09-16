# Shortwave Dashboard Design

## Goal

Make the Tab5 a useful, approachable shortwave receiver with a dedicated LIVE
surface and honest receiver state, while preserving direct controls for
experienced listeners. The longer product flow is tune, discover, identify,
listen, remember, and log.

## Phase 1 scope

- Dedicated Shortwave module and screen lifecycle.
- AM reception from 1.71 to 30 MHz using the existing receiver, DSP, audio,
  spectrum, recording, storage, and device-capability services.
- ITU broadcast-band awareness with 120 m through 11 m labels.
- 100 Hz, 500 Hz, 1 kHz, and 5 kHz tuning steps.
- Narrow, Normal, and Wide filter presets backed by existing numeric bandwidths.
- Capability-aware tuning controls that distinguish RF gain, tuner AGC, RTL
  AGC, audio boost, volume, and offset tuning.
- Honest route labels: DIRECT Q, HF UPCONVERTER, or TUNER.
- Station-card, memory, and listener-log data contracts that allow missing and
  unidentified station information.

MEMORY, LOGBOOK persistence/export, ON AIR schedules, HUNT scanning, ADIF,
propagation, maps, and additional demodulation modes remain later phases.

## Module ownership

- `shortwave_model.*`: international band table, band lookup, receiver/tuning
  presentation state, and station/memory/log data contracts.
- `receiver_tuning_controls.*`: shared capability-aware labels and touch model.
  Phase 1 uses it in Shortwave; AM/FM adoption follows as a focused migration.
- `shortwave_dashboard.*`: all Shortwave drawing, lifecycle, spectrum drawing,
  and touch-to-action translation.
- `main.cpp`: a small adapter that supplies a snapshot, executes returned
  receiver actions, forwards spectrum/touch events, and registers self-checks.

No receiver implementation, scan engine, database parser, or persistence logic
lives in `main.cpp`.

## Receiver integration

Add `radio::Band::shortwave` so shared receiver code can select existing AM
demodulation without inheriting medium-wave frequency limits. Shortwave uses the
same audio AGC/limiter and numeric filter machinery as AM. Device capabilities
and current frequency decide whether tuner controls are actionable.

- Blog V3c below 24 MHz: DIRECT Q; tuner gain and tuner AGC unavailable; audio
  boost and volume remain available.
- Blog V3c at or above 24 MHz: TUNER; manual RF gain may be available.
- Blog V4 below 28.8 MHz: HF UPCONVERTER; tuner gain and tuner AGC remain valid.
- Unsupported capabilities are visibly unavailable and never dispatched.

`TUNER AGC` is the V4 default. `AUDIO BOOST` is post-demodulation software gain,
not an RF claim. Offset tuning is an advanced tuning control, not gain.

## UI

LIVE is the only functional Phase 1 tab. The bottom bar shows LIVE, ON AIR,
HUNT, MEMORY, and LOGBOOK; future tabs are visibly labeled as later work rather
than presenting fake controls. Frequency, broadcast band, AM mode, filter,
route, spectrum, signal level, tuning step, and tuning controls are glanceable
at 1280 by 720 with existing M5GFX conventions and touch sizes.

Station information is absent until a real station card exists. Future matches
must use POSSIBLE, LIKELY, or SCHEDULE MATCH and never claim decoded identity.

## Data contracts

Fixed-size embedded-friendly structures represent optional station fields,
memories, and log entries. Empty strings and zero values mean unavailable.
Logs preserve UTC, local offset, exact frequency, mode, bandwidth, measured
signal, station/program, optional callsign/country/language/antenna/device,
notes, and an optional recording path. Audio remains a referenced SD file.

## Evidence

PC oracle evidence on 2026-09-14 around 23:00 Pacific:

- Blog V3c + MLA-30+ + Q direct sampling found credible 49 m RF candidates.
- 5935 kHz produced quiet but user-confirmed music after software gain.
- Blog V4 + MLA-30+ at 5935 kHz produced user-confirmed voice; the user judged
  tuner AGC probably best. The language sounded Spanish but was not verified.

These are reception observations, not station identifications.

The band table follows the ITU terrestrial broadcasting allocations. ADIF
3.1.7 is the future SWL interchange target; CSV remains the first universal
listener-log export.

## Testing and acceptance

- Host/self-check band edges, gaps, steps, filter presets, data validation,
  capability labels, and touch actions.
- Existing UI self-check remains green.
- Native ESP-IDF build must pass before flashing.
- Source, build, flash, serial, IQ, audio, and physical UI are separate claims.
- The user has authorized flashing after clean Phase 1 build/self-checks.
- V4 + dipole hardware testing cannot reject reception quality because that
  antenna is not established as suitable for the target shortwave signal.

## Sources

- ITU, Frequency Bands Allocated to Terrestrial Broadcasting Services:
  https://www.itu.int/en/ITU-R/terrestrial/broadcast/Pages/bands.aspx
- ADIF 3.1.7 specification index: https://www.adif.org.uk/317/index.htm
- Osmocom rtl-sdr direct sampling API and tools:
  https://github.com/osmocom/rtl-sdr
