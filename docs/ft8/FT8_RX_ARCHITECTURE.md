# FT8 RX architecture

## Design rule

FT8 is not implemented as a special case inside `main.cpp`. The receiver, frontend, protocol detector, decoder, Hunter state machine, and dashboard have explicit seams.

## Proposed pipeline

```text
RTL-SDR CU8 IQ
    |
    v
FT8 receiver route
    |
    +-- frequency/gain/continuity metadata
    v
FT8 narrow USB frontend
    |
    +-- channel filter
    +-- decimation/resampling
    +-- USB/baseband extraction
    v
12 kHz mono analysis stream
    |
    +--> slot accumulator
    +--> low-cost activity/signature detector
    |
    v
FT8 decoder backend
    |
    +-- synchronization candidates
    +-- 8-FSK likelihoods
    +-- LDPC
    +-- CRC
    +-- message unpack
    v
validated Decode records
    |
    +--> LIVE
    +--> DECODES
    +--> MAP
    +--> HEARD
    +--> HUNTER scoring
```

## Why a dedicated frontend

OrcSDR starts from RTL-SDR IQ rather than sound-card audio. FT8 therefore needs a purpose-built receiver path instead of depending on the generic Shortwave speaker path.

The existing DSP audit notes limitations in the current general SSB path. FT8 should not inherit a weak/incorrect sideband implementation merely because its conventional operating mode is USB.

The FT8 frontend should target the analysis stream the decoder actually needs and avoid unnecessary speaker-audio work.

## Decoder backend seam

The dashboard depends only on `ft8_decoder_backend.hpp`.

The first benchmark backend may use an existing embedded FT8 implementation such as `ft8_lib`, provided licensing and integration requirements are satisfied. This is a benchmark/reference decision, not a permanent architectural dependency.

A later OrcSDR-native decoder can implement the same backend interface and be compared on identical recordings.

## Detector seam

Hunter needs the protocol detector independently from full decode.

The detector should expose observations such as:

- noise/energy metric;
- count or strength of credible FT8 synchronization candidates;
- whether the evidence crossed the FT8-signature threshold;
- completed valid-decode count.

This permits FAST HUNT to avoid paying for full deep decoding on every quiet band.

## Timing

FT8 receive timing depends on accurate UTC slot boundaries.

The FT8 runtime must explicitly represent:

- wall-clock valid/invalid;
- current 15-second slot;
- receiver settled/unsettled;
- discontinuity/retune events;
- slot complete/incomplete.

An incomplete slot after tune, sample loss, or clock discontinuity is not treated as a normal decode failure.

## Band presets

Initial Hunter uses a bounded built-in set of conventional FT8 dial frequencies. Presets are data, not protocol truth, and can be revised independently of the decoder.

The scanner should permit users to include/exclude bands without requiring network access.

## Memory and scheduling

The ESP32-P4/Tab5 has enough compute and PSRAM to make on-device FT8 practical, but OrcSDR must benchmark the complete pipeline rather than assume the decoder alone is cheap.

Important measurements:

- frontend CPU time;
- detector CPU time;
- full decode CPU time;
- peak internal SRAM;
- peak PSRAM;
- missed IQ blocks;
- slot completion latency;
- UI responsiveness;
- valid decodes versus a desktop reference.

The long-term RF scheduler may allow opportunistic FT8 patrol while the tuner is otherwise idle. This is future work and must never pretend one dongle can continuously monitor mutually exclusive frequencies.

## Persistence

Core FT8 operation is offline.

Potential later session logging belongs under a dedicated OrcSDR FT8 storage path and should follow existing atomic-write conventions. A dashboard screenshot or transient RAM list is not durable logging.

## Safety boundary

Initial feature scope is RX only.

No CAT control, PTT, waveform generation, transmit timing, transmit frequency, power control, or automatic QSO behavior belongs in the initial architecture.
