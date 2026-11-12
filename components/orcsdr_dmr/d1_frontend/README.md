# OrcSDR DMR D1-P — fixed-memory 4FSK front-end with first-burst pre-roll

**Status: synthetic, host-verified Phase D1-P. Not real-IQ qualified and not integrated into firmware.** This directory owns only the 48 kS/s channelized complex-IQ to 4,800-symbol/s hard-dibit front-end. The D0 protocol constants, D1 protocol decoder, D1 integration bridge, and firmware/UI are all read-only dependencies.

## What D1-P changes

The previous D1 receiver acquired a 24-dibit DMR sync and **discarded that sync and the 54 preceding payload dibits**. Its output began with the next symbol, which meant the existing D1 bridge could not decode the *first* 132-dibit burst of an isolated transmission.

D1-P now retains a fixed **1,024-sample discriminator history** and, upon a qualified ETSI D0 sync, replays the following **78 measured symbol centers** in transmitted order:

| Burst dibit positions | Source | Count |
|---|---|---:|
| 0–53 | First payload half, sliced from retained IQ discriminator history | 54 |
| 54–77 | Actual 48-bit DMR voice/data sync, sliced from the same measured history | 24 |
| 78–131 | Remaining burst payload, from continuing live IQ | 54 |

This is **not** a fixed sync-word injection or reconstructing data from a known test vector. The returned `Symbol.dibit` and `Symbol.error_hz` are calculated from measured filtered discriminator samples, including the 24 sync dibits; `Symbol.sample_index` is the actual selected matched-filter center relative to the latest reset/discontinuity. D0 sync constants are used read-only to *select and calibrate* an acquisition, not to fill or overwrite any output.

The pre-roll is a cursor into the bounded history ring, **not a second bit array or heap queue**. During pre-roll it emits one symbol after consuming at least one new caller-provided IQ sample; post-sync tracking catches up using retained samples before continuing normally. This preserves the existing D1-C bridge's single-output, at-most-eight-input-sample call pattern. `Frontend::process()` and `Symbol` remain source/API compatible with the existing bridge; three counters are appended to `Stats`.

### Missing samples are NOT invented

A qualified sync is accepted only if **every one of the first 78 symbol centers and its full 61-tap discriminator-filter history** is backed by captured IQ since the last reset/discontinuity. If the capture begins mid-burst or too close to the beginning of the first symbol for complete FIR support, the candidate is rejected. There is no zero-fill, guessed dibit, or synthetic preamble. `Stats::incomplete_preroll_rejections` counts such sync candidates. The next independently qualified sync may be acquired when sufficient real history exists.

The test waveform includes **130 physical lead-in IQ samples** before the first symbol; this is *measured IQ*, not an output pre-roll manufactured by the decoder. A capture beginning at the burst's first sample without enough filter lead-in is conservatively rejected, even when some of its payload might have been recoverable by an unconstrained detector. A correct sync correlation is evidence of synchronization **only**. It does not validate a DMR payload, CRC, slot, identity, or site.

## Decoder and streaming contract

- **Input:** caller-owned 48,000 CF32 complex samples/second (`ComplexSample{re,im}`), already tuned/channelized.
- **Output:** hard dibits 0–3 in transmitted order, nominal 4,800 dibits/second; every `Symbol` includes signed slicing residual and relative RX sample-center timestamp.
- **Levels:** ETSI TS 102 361-1 Table 10.3: `00=+648 Hz`, `01=+1944 Hz`, `10=−648 Hz`, `11=−1944 Hz` before arbitrary carrier offset. Eight D0 ETSI voice/data sync words are matched read-only.
- **DSP:** conjugate-sample FM discriminator, 61-tap RRC frequency filter, affine sync fit to estimate carrier offset and deviation gain, 4-level slicer, transition-based timing loop, slow CFO tracking, lock-loss handling.
- **Fixed storage:** no `new`, `malloc`, `std::vector`, or owning caller pointers inside the receiver. A 1,024-float ring holds measured frequency samples; no per-symbol heap buffering.
- **Backpressure:** caller controls output capacity. When full, `process()` stops consuming IQ; retry the unconsumed tail. A zero-capacity call consumes zero IQ and emits zero symbols. A pre-roll output consumes at least one IQ sample so the existing bridge cannot see `consumed==0` and report a programmer error.
- **Discontinuities:** `discontinuity()` clears phase, filter, lock and pending replay immediately; counters remain. `reset()` clears counters as well. Invalid, non-finite, or too-weak IQ clears DSP state rather than connecting different captures together.
- **Stats:** lifetime samples/symbols, locks/losses, invalid IQ, sync correlation/kind, offset, deviation gain, timing and slicer RMS; D1-P adds `preroll_symbols`, `full_preroll_acquisitions`, `incomplete_preroll_rejections`. A completed full pre-roll is **not proof** that a valid payload/identity was decoded.

### Calling from existing D1-C

The original D1 integration bridge already feeds one `Symbol` into the protocol stream whenever the front-end is locked. It clears the protocol decoder on acquisition, which is correct **before** the first D1-P pre-roll symbol arrives. The first 132 emitted dibits can now constitute a complete, sync-positioned burst without modifying the bridge's `process()` interface, callback, symbol timestamp semantics or protocol source.

**Known out-of-scope integration issue:** `components/orcsdr_dmr/d1_integration/src/d1_bridge.cpp` currently increments `stats_.acquisition_bursts_missed` on **every** acquisition, even when D1-P has physically recovered the full burst. Its comment claiming the front-end always starts *after* sync is also now stale. Neither was changed in this phase due to strict folder ownership; treat that statistic as outdated until bridge-side follow-up verification/revision. This front-end change alone is **not** evidence of end-to-end D1 link-control identity decoding or CACH slot confirmation.

## Reproduce the tests

From the **OrcSDR repository root**, CMake ≥3.16 and C++17 GCC/Clang/MSVC:

```sh
cmake -S components/orcsdr_dmr/d1_frontend -B build/dmr-d1p -DCMAKE_BUILD_TYPE=Release
cmake --build build/dmr-d1p --parallel
ctest --test-dir build/dmr-d1p --output-on-failure -V
```

This runs the **unchanged original `dmr_d1_frontend` test** and the new `dmr_d1_preroll` test. Preserve a local log beside the invocation if desired:

```sh
ctest --test-dir build/dmr-d1p --output-on-failure -V > dmr-d1p-test.log 2>&1
```

ASan/UBSan (GCC/Clang):

```sh
cmake -S components/orcsdr_dmr/d1_frontend -B build/dmr-d1p-sanitize \
  -DCMAKE_BUILD_TYPE=Debug -DDMR_D1_SANITIZE=ON
cmake --build build/dmr-d1p-sanitize --parallel
ctest --test-dir build/dmr-d1p-sanitize --output-on-failure
```

The new test independently synthesizes **exactly one 132-dibit burst**, with sync in dibits 54–77 and arbitrary unknown payload elsewhere, then modulates IQ with an independent pulse and phase integrator. It does not inject a known output bitstream into receiver state. It checks all eight sync variants, 132 returned first-burst dibits, 78 pre-roll symbols, strict increasing sample timestamps, identical results across 511-sample/1-sample/7-sample chunks with capacities 256 and 1, carrier offsets, IQ AWGN, fractional timing shifts, ±350 ppm clocks, false locks from no-sync random FSK and 48,000 AWGN IQ samples, capture starting at or after the true burst start, zero-output backpressure, and dropping a partially replayed pre-roll on discontinuity. The existing test still covers long sessions, false CW, wider invalid inputs, and throughput.

## D1-P measured baseline — 2026-11-11

Host: Linux x86-64, GCC `-O3` CMake Release, deterministic synthetic waveforms; reproduced with Clang Release and GCC Debug ASan/UBSan. **All tests passed** in all three configurations.

| One-burst synthetic fixture | First-burst symbol errors | Recovered | Measured outcome |
|---|---:|---|---|
| Eight independent D0 sync kinds, +320 Hz CFO, low AWGN | 0 / 132 each | All 132 each | Sync bits exact |
| +1,750 Hz CFO, IQ noise sigma 0.06, +7.1-sample shift | 0 / 132 | All 132 | Sync bits exact |
| −2,700 Hz CFO, IQ noise sigma 0.03, +5.1-sample shift | 0 / 132 | All 132 | Sync bits exact |
| +950 Hz CFO, +350 ppm clock, IQ sigma 0.015 | 0 / 132 | All 132 | Sync bits exact |
| −1,350 Hz CFO, −350 ppm clock, IQ sigma 0.02, +8.6-sample shift | **6 / 132** | All 132 | Sync has errors; no correction falsely claimed |
| No captured FIR lead-in / capture starts mid-burst | No emitted burst | **Rejected** | No invented payload |
| No-sync random 4FSK / 48,000 complex AWGN samples | 0 observed false locks | N/A | Finite tests only |

The high-impairment case is deliberately retained rather than silently described as zero-error. Errors are **hard-symbol errors**, not BER estimates from real RF. A valid matched sync does **not** turn hard-sliced bits into an integrity-verified burst.

| Measurement | Host result |
|---|---:|
| `sizeof(Frontend)` (`static_storage_bytes`) | **4,784 bytes** |
| `sizeof(D1-C Receiver)` with unchanged bridge/D1 type layouts | **7,664 bytes** (below existing 8 KiB test gate) |
| Previous `sizeof(Frontend)` (D1 baseline) | 2,696 bytes |
| Increase | 2,088 bytes |
| Legacy repeated 1,524,000-IQ-sample benchmark, Release | ~7.90 million IQ samples/sec (~164.7× input realtime) |

Values are host/compiler/ABI-specific; no ESP32-P4 stack, heap, CPU, or energy measurement is implied. Production linked bridge conformance and recorded live IQ still require their own acceptance gates.

## Boundaries / follow-up

1. No edits to D0, D1 protocol, D1 integration, P25, firmware or UI.
2. No real captured DMR IQ, adjacent-channel blockers, weak-RF long-term false alarm probabilities, or device profiling in this phase.
3. In unusual strong noise/timing offsets, individual sync bits can be hard-sliced incorrectly despite a correlation lock. Do not fabricate corrected sync or attach unverified identity.
4. D1-C should update its stale `acquisition_bursts_missed` statistic and first-burst documentation after a separate linked acceptance test, but **that is not part of this commit**.

Specification basis: [ETSI TS 102 361-1](https://www.etsi.org/deliver/etsi_ts/102300_102399/10236101/02.07.01_60/ts_10236101v020701p.pdf), §9.1/Table 9.2 and §10.2.2.1/Table 10.3. No competitor implementation used.
