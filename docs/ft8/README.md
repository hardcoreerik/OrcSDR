# OrcSDR FT8 RX

Status: **receive implementation on main** (FT8, FT4 and JS8 Normal).

The original design notes below are retained. Current JS8 frame coverage and evidence limits are in [JS8 status](../js8/STATUS.md); the [review completion](REVIEW_COMPLETION_2026-10-09.md) records the follow-up fixes and hardware acceptance boundary.

FT8 support is receive-only for the initial OrcSDR implementation. The feature is designed as an offline-first, touch-first FT8 receiver for the M5Stack Tab5 and RTL-SDR-class hardware. It must not imply a decode, station identity, location, or protocol classification that the receiver did not actually establish.

For setup and the dated four-receiver reception check, see the [FT8 user guide](../user-guide/dashboards/ft8.md).

## Product goals

- One-touch FT8 reception for users who do not already know amateur-radio operating conventions.
- Manual FT8 band selection for experienced users.
- **FT8 Hunter** that can search conventional FT8 activity frequencies and identify the best active band.
- A clear 15-second receive/decode rhythm.
- On-device decode history and station-heard views.
- Offline Maidenhead locator mapping when a decoded message actually contains a valid locator.
- No Internet dependency for core receive, scan, decode, history, or map behavior.
- RX only until a separate transmit design and safety review exists.

## Dashboard

The planned tabs are:

1. **LIVE** — current band, slot clock, waterfall/spectrum activity, decoder state, and newest decodes.
2. **DECODES** — session decode table with UTC, SNR, DT, audio offset, type, message, and grid.
3. **MAP** — offline display of station-reported Maidenhead locators.
4. **HUNTER** — FT8 band discovery and activity ranking.
5. **HEARD** — unique decoded callsigns and recent reception facts.
6. **SETUP** — decoder, clock, gain, receiver, logging, and Hunter controls.

Manual band presets remain available from HUNTER so discovery and direct tuning share one screen.

## Truth model

Hunter and the decoder must keep these observations distinct:

- **QUIET** — no useful activity above the configured detector threshold.
- **ENERGY** — RF/audio energy exists in the expected passband.
- **FT8 SIGNATURE** — synchronization/tone evidence is consistent with FT8.
- **VALID DECODE** — an FT8 frame passed decoder validity checks and produced a message.

Energy alone is never presented as FT8. A locator is never presented as a measured station location; it is a station-reported locator decoded from a message.


## Host validation

Run the FT8 model, Hunter, and OrcDial semantic-control regression suite with:

```bash
bash tools/test-ft8.sh
```

On the project's normal Windows + WSL development environment:

```powershell
.\tools\test-ft8.ps1
```

The suite builds optimized binaries and AddressSanitizer/UndefinedBehaviorSanitizer variants for the pure FT8 model, Hunter state machine, and OrcDial FT8 semantic controller. Device-level M5GFX, physical OrcDial transport, and RF/DSP validation remain separate gates.

## Related design documents

- [FT8 dashboard vision](FT8_DASHBOARD_VISION.md)
- [FT8 RX architecture](FT8_RX_ARCHITECTURE.md)
- [FT8 implementation plan](FT8_IMPLEMENTATION_PLAN.md)
- [FT8 OrcDial design](FT8_ORCDIAL_DESIGN.md)
- [Native decoder research](DECODER_RESEARCH.md)
- [Multi-mode profile design](MODE_PROFILE_DESIGN.md)
- [Decoder seam proposals](DECODER_SEAM_PROPOSALS.md)
- [12 kHz USB analysis tap proposal](AUDIO_TAP_PROPOSAL.md)
- [Native decoder implementation notebook](PHASE1_IMPLEMENTATION.md)
- [Real WAV reference benchmark](REAL_WAV_BENCHMARK.md)

The external WAV validation workflow runs standard WSJT-X `jt9` and the native benchmark on the same downloaded audio. The primary FT8 and FT4 WAV hashes are pinned; each run saves all sample hashes, the reference package version and both decoder outputs. This regression floor does not claim WSJT-X coverage parity or hardware acceptance. The native-core path coverage from PR #172 is folded into the existing FT8 host-test workflow, avoiding a duplicate full-suite job.

## Multi-mode seam (FT8 / FT4 / JS8Call)

The dashboard, model and decoder backend contract are mode-aware, and FT8 stays the default with unchanged behavior.

- `DigitalMode` (`ft8_model.hpp`): FT8, FT4, JS8 Normal, JS8 Fast, JS8 40, JS8 Slow, JS8 60 (experimental: the
  specification is unpublished). `Decode` gains `mode` (default FT8) and `flags` (assisted/AP, hash-resolved,
  multi-frame); both are appended after the original fields. `DecodeKind` keeps its meaning.
- Slot length is data-driven: `slot_ms(mode)` is 15000 (FT8), 7500 (FT4), 15000 / 10000 / 6000 / 30000 / 4000 (JS8
  Normal / Fast / 40 / Slow / 60). `slot_clock(utc_ms, mode)` defaults to FT8, and the LIVE ring and countdown use the
  selected mode's period.
- `DecoderBackend` (`ft8_decoder_backend.hpp`) gains OPTIONAL tail callbacks `set_mode`, `begin_slot_ms` (FT4's 7.5 s
  slots do not start on whole seconds) and `capabilities`. The five required callbacks and `backend_valid()` are
  unchanged. A backend without `capabilities` is treated as FT8-only; no backend at all is UNBOUND exactly as before.
  Capability bits (`decoder_cap_ft8` 1, `ft4` 2, `js8` 4, `assisted` 8, `message_assembly` 16) match the decoder
  workstream's proposal. Helpers: `backend_capabilities`, `mode_supported`, `backend_set_mode`, `backend_begin_slot`.
- SETUP has a mode selector. A mode is selectable only if the bound decoder reports it; everything else is shown as
  UNAVAILABLE (JS8 60 as EXPERIMENTAL even when supported). While no decoder is bound only FT8 is selected. LIVE shows
  the current mode in the MODE chip. Selecting a mode never makes it operational.
- Only FT8 has a verified band table. For any other mode the dial and band label show a pending marker rather than
  FT8's frequencies.
- JS8 assembled multi-frame messages are not `Decode` records. Their data contract is intentionally not defined yet;
  a MESSAGES view will be added once the decoder-side message record is stable.

OrcSDR is receive-only. Any future transmit work would be framework only and remain fully unimplemented and
unsupported; nothing in this seam transmits.
