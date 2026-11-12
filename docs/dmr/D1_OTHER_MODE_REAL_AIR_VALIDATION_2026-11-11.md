# OrcSDR DMR — real-air Tier III control and voice terminator: independent host replay

**Measured:** 2026-11-12 UTC (2026-11-11 America/Los_Angeles).  
**Branch:** `feature/dmr-digital-voice-foundation`.  
**Evidence:** [Before correction: CI run 43179419839](https://github.com/hardcoreerik/OrcSDR/actions/runs/43179419839) · [Power-floor diagnosis: CI run 43179812439](https://github.com/hardcoreerik/OrcSDR/actions/runs/43179812439) · [After correction: CI run 43180157835](https://github.com/hardcoreerik/OrcSDR/actions/runs/43180157835).  
**Repeatable job:** [`.github/workflows/dmr-tier3-voice-real-iq.yml`](../../.github/workflows/dmr-tier3-voice-real-iq.yml).

## Why this matters

The original D1 acceptance used two clips from **one direct-mode handheld session**. It did not prove that the receiver could decode radio traffic from a **separate Tier III control and voice carrier**. Both additional captures are genuine, channelized 48-kS/s CF32 little-endian recordings from MattCheramie/GopherTrunk at pinned source commit `f06bfff493f47fdeb69807c8b08b2b32a253839f`; both already appear in `tests/digital_voice_replay/manifest.json`.

The new **non-gating** CI job downloads recordings into runner temporary storage, verifies exact bytes plus Git blob SHA-1 and raw SHA-256, builds the actual OrcSDR native frontend + D1 bridge, and feeds each recording at **1, 7, 257, 4096** samples per input chunk. It saves **only small JSON reports**, not raw IQ. Host tests pass in that job, and a separate [Release/ASan CI 43180163089](https://github.com/hardcoreerik/OrcSDR/actions/runs/43180163089) now runs both the D1 bridge and standalone 4FSK/pre-roll tests.

| Public recorded IQ | Samples | Byte length | Verified Git blob SHA-1 | Verified raw SHA-256 |
|---|---:|---:|---|---|
| Tier III control `dmr-t3-cc.cfile` | 62,400 | 499,200 | `7a119ec16e5c6412488f2ed112b2c5b85cdd799f` | `08d1697bc2c53721be1c3071f34760d03f3274bc1b85432b48273421495ffbf0` |
| Voice terminator `dmr-voice-term.cfile` | 67,200 | 537,600 | `91620e07e5350af6bce85d75152c23c62bfed253` | `5b442ae37a40d1344a7be92130812ae099c9f683a21ee338c70e5d7599efd0c9` |

## Actual decoder results — after fix

| Metric | Tier III control IQ | Voice-terminator IQ |
|---|---:|---:|
| Sync events | **42** | **46** |
| Burst events | **42** | **46** |
| CRC-16-validated events | **21** | 0 |
| RS(12,9)-validated events | 0 | **23** |
| FEC + independent integrity validated events | **21** | **23** |
| Validated source radio | Not reported | **4209000** |
| Validated talkgroup | Not reported | **24** |
| Decoded color code | **0** | **12** |
| Ordered metrics / event hash equal across four input chunk sizes | **PASS** | **PASS** |

**Important interpretation:** the Tier III sample demonstrates real-air **CSBK packet FEC+CRC**, not Tier III control-channel *semantics* such as ALOHA opcode classification, system/site acquisition or voice-channel following. The voice-terminator sample demonstrates **real-air Full LC BPTC + RS + group/source metadata**, *not* speech decoding. Upstream documented source/group `4209000/24` are used only as an independent post-decode comparison, not injected into the OrcSDR parser.

## Confirmed bug: absolute IQ amplitude cutoff prevented lock

Before this correction the native receiver decoded **zero** voice-terminator syncs and zero bursts from the exact same verified recording. Investigation measured:

- Tier III control IQ RMS amplitude ≈ **0.01857**, peak ≈ **0.02023** (passes earlier input power gate).
- Voice terminator IQ RMS amplitude ≈ **0.00115**, peak ≈ **0.00188**.
- The frontend's `kMinPower = 1e-5` rejected any complex sample where `I² + Q² < 1e-5`, which means amplitude below **0.003162**. Even the voice terminator's *peak amplitude* was below that limit; every sample was rejected.
- Conjugating the IQ (flipping complex frequency-discriminator polarity) did not recover a single burst before the fix; **amplitude floor, not a polarity bug**, was the confirmed primary blocker.

**Correction:** `components/orcsdr_dmr/d1_frontend/src/frontend.cpp` now uses `kMinPower = 1e-10` as a **numerical floor**, not an RF squelch threshold. The phase discriminator is naturally amplitude-independent for nonzero, finite IQ. No direct change to the protocol parser, ETSI data, FEC, CRC, RS, CACH confidence, RTL-SDR USB driver, P25, or UI was required.

**Regression tests:** frontend synthetic IQ scaled by **0.001×** and **0.0001×** of original amplitude retains 4FSK acquisition and identical decisions to the high-amplitude reference, with **zero invalid IQ samples**. [Release and ASan/UBSan CI](https://github.com/hardcoreerik/OrcSDR/actions/runs/43180163089) passed the standalone frontend and pre-roll suites, plus D1-C integrated bridge tests.

The original two **direct-mode real-air** recordings remain accepted by [CI 43180098136](https://github.com/hardcoreerik/OrcSDR/actions/runs/43180098136): first 11 syncs / **10** FEC+integrity-checked burst events, second 13 syncs / **10** checked events; both recover radio `3024109`, TG `99`, CC `1`, and 257/4096 chunk invariance.

## Remaining limits / next action

1. The separately started **D1-N agent** owns exact pinned noise and AM negative-control replay. A low numerical IQ floor does **not** constitute validated RF squelch or false-alarm safety; only independent integrity-validated traffic may become radio identities. Leave its four-fixture gate open until the agent supplies actual results.
2. **D1-F agent** independently attacks the new one-bit data sync tolerance and CACH confidence. This standalone positive-corpus result is not an adversarial false-alarm measurement.
3. **D1-H agent** is preparing physical ESP32-P4 profiling; no actual Tab5/USB SDR measurements exist yet.
4. Full Tier III signaling (ALOHA, trunk site/network identifiers and grants) has **not** been implemented or claimed. Tier III control CRC-valid CSBKs show shared physical-layer usefulness, not trunk-controller functionality.
5. These recordings are only two independent control/voice channels plus two direct-mode cuts. They do **not** calibrate weak-signal sensitivity, floor/noise behavior, field capture reliability or radio diversity broadly.

**Exit result for this lead-owned research gap:** all **four public, source-verified DMR recordings** have now been run through the **native OrcSDR decoder**. A/B yield group/source IDs, C yields CSBK FEC+CRC, and D yields Full LC RS and radio/talkgroup IDs. Full D1-R acceptance, actual device qualification, broader corpus and dashboard remain open.
