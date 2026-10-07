# FT8 RX implementation plan

Status: **sandbox plan**

Target branch: `codex/ft8-rx-dashboard-sandbox`

The plan intentionally separates visual/dashboard work, RF frontend work, protocol detection, and full decoding so each layer can be tested honestly.

## Phase 0 - documentation and contracts

Deliverables:

- FT8 product vision and truth model.
- Dashboard tab structure.
- Hunter lifecycle and observation states.
- Decoder backend interface.
- Host-testable FT8 model helpers.
- No claim of live FT8 decoding.

Gate:

- Documentation matches the intended RX-only product.
- No `origin/main` changes.
- Dashboard can remain explicitly UNBOUND.

## Phase 1 - dashboard and Hunter state model

Deliverables:

- LIVE / DECODES / MAP / HUNTER / HEARD / SETUP.
- Hunter state machine with deterministic mock/synthetic observations.
- Per-band result model.
- FAST HUNT and DECODE HUNT modes.
- Start/stop/lock-best actions.
- Touch navigation and focus-nav support.
- Host tests for band sequencing, state transitions, scoring, and truth labels.

Gate:

- UI never labels raw energy as FT8.
- Hunter can be exercised without RF hardware.
- 1280 x 720 controls do not overlap.
- No major UI code is placed in `main.cpp`.

## Phase 2 - reference decoder benchmark

Deliverables:

- Build a known FT8 decoder backend on host first.
- Decode known-good 12 kHz WAV test vectors.
- Establish reference output and timing.
- If an external implementation is used, record exact license and upstream revision.
- Add deterministic regression fixtures where redistribution is permitted.

Gate:

- Known FT8 messages decode correctly.
- Corrupt/non-FT8 fixtures do not produce accepted messages.
- Decoder reports validity through CRC/message checks.
- Performance metrics are captured.

## Phase 3 - OrcSDR FT8 RF frontend

Deliverables:

- Dedicated CU8 IQ -> FT8 analysis path.
- Proper channel filtering and USB/baseband extraction.
- Controlled resampling to the decoder input rate.
- Retune/sample-discontinuity reset behavior.
- Saved-IQ replay path for repeatable testing.

Gate:

- Synthetic tone and FT8 vectors survive the RF/frontend path.
- No unexplained sample-rate drift.
- No DSP watchdog resets.
- Frontend stays within measured Tab5 realtime budget.

## Phase 4 - live single-band FT8

Deliverables:

- Manual band tune.
- UTC aligned slot capture.
- Live FT8 decode.
- LIVE waterfall/activity and decoder-state updates.
- DECODES / HEARD / MAP fed exclusively by valid decoder results.

Gate:

- Live antenna reception is demonstrated independently from synthetic/replay tests.
- Valid decodes compare reasonably with a desktop reference on the same RF capture.
- Incomplete slots are not reported as ordinary decode misses.

## Phase 5 - FT8 Hunter

Deliverables:

- conventional FT8 preset sequence;
- tuner settle handling;
- slot-aligned observation;
- FAST HUNT signature detection;
- DECODE HUNT full decode;
- per-band score/history;
- best-band recommendation;
- user stop and lock controls.

Gate:

- Quiet, energy-only, signature, and valid-decode states are distinguishable in tests.
- Hunter never claims FT8 based on energy alone.
- Hunter can complete a selected-band sweep without tuner/DSP instability.
- Locking the best band returns cleanly to LIVE.

## Phase 6 - quality and deeper decoding

Possible work:

- improve synchronization search;
- improve soft-bit likelihoods;
- tune LDPC iterations;
- multi-pass candidate search;
- strong-signal subtraction / interference cancellation;
- compare an Orc-native decoder against the reference backend.

All improvements remain benchmark driven.

## Test matrix

### Host

- slot-clock boundary tests;
- band-table tests;
- Maidenhead validation/conversion;
- decoded-message classification;
- Hunter transition tests;
- Hunter scoring tests;
- decoder test vectors;
- malformed/non-FT8 rejection.

### Saved RF

- known FT8 IQ capture;
- weak FT8 capture;
- busy multi-signal capture;
- adjacent/interfering signal capture;
- retune/discontinuity capture;
- no-signal/noise capture.

### Tab5 live

Measure:

- IQ drops;
- task watchdog resets;
- DSP load;
- decoder duration;
- peak internal RAM;
- peak PSRAM;
- UI responsiveness;
- valid messages per slot.

## Completion definition for initial RX release

The feature is ready for integration when:

1. a user can open FT8, select a band, and receive valid FT8 decodes;
2. Hunter can find and rank active FT8 bands without false protocol claims;
3. MAP only plots station-reported decoded locators;
4. the feature works without Internet;
5. the Tab5 remains stable during repeated multi-band Hunter sweeps;
6. results are benchmarked against recorded reference captures;
7. documentation clearly separates measured RF, detected FT8 signature, decoded message, and interpreted metadata;
8. transmit functionality remains absent.
