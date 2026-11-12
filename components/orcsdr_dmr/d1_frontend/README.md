# OrcSDR DMR D1 — fixed-memory 4FSK front-end (host-only)

**Status:** independently implemented and host-tested; **not** connected to OrcSDR firmware, a DMR burst decoder, a physical receiver, or any on-air validation. This folder belongs only to the `D1` radio front-end workstream. It does not change D0, P25, the application, or the device radio session.

## Contract

`include/frontend.hpp` provides `orcsdr::dmr::d1::Frontend`. Input is **channelized 48,000 complex float32 IQ samples per second**, in a caller-owned array of `{float re, float im}` (real first, imag second). Output is a caller-owned `Symbol` array containing **ETSI Table 10.3 dibits (`0,1,2,3`) at nominal 4,800 symbols/s after lock**, signed frequency-slicer residual, and sample-center index. There are no heap allocations or STL dynamic containers in receiver code.

- Dibit `01` → `+1944 Hz` (`+3`); `00` → `+648 Hz` (`+1`); `10` → `-648 Hz` (`-1`); `11` → `-1944 Hz` (`-3`). Uses ETSI TS 102 361-1 V2.7.1 clause 10.2.2.1, Table 10.3. [Official ETSI spec](https://www.etsi.org/deliver/etsi_ts/102300_102399/10236101/02.07.01_60/ts_10236101v020701p.pdf).
- Takes the phase difference of successive complex samples (`arg(conj(prev) × current)`) to obtain instantaneous frequency, independent of input gain above its finite/minimum-amplitude guard.
- Uses a six-symbol, 61-tap, rolloff-0.2 root-raised-cosine receive pulse to smooth frequency discriminator output; coefficients calculated once when the receiver is constructed. **The host waveforms use corresponding independent transmitter pulse synthesis; no unverified claim is made for other transmit filtering or adjacent-channel interference.**
- Detects the 24-dibit voice/data SYNC sequences through the existing **read-only D0** `../include/orcsdr_dmr/d0.hpp` constants. A 24-symbol affine fit simultaneously estimates frequency offset and deviation scale and scores normalized correlation. Constrains deviations to 400–950 Hz per level and offset to ±4 kHz, with sync score threshold 0.955. D0 code is not modified or copied.
- After acquisition, performs 4-level slicing, slow decision-directed carrier tracking and a transition-centered timing loop with proportional and integral correction. The bounded nominal 10-sample step is designed for moderate clock drift and tested at ±350 ppm.
- Requires a qualifying sync before output and starts output at the **next symbol after sync**; it deliberately does **not** backfill the 24 sync dibits. **A sync match is not a validated DMR message, verified site, or CRC.** No CACH, FEC, slot framing, IDs, voice, encryption, protocol attribution beyond sync type, or RF evidence is implemented here.

### Streaming API and output sizing

```cpp
orcsdr::dmr::d1::Frontend rx;
orcsdr::dmr::d1::Symbol decisions[64];
std::size_t remaining = iq_count;
const orcsdr::dmr::d1::ComplexSample* cursor = iq;
while (remaining > 0) {
    auto step = rx.process(cursor, remaining, decisions, 64);
    // Consume decisions[0 .. step.produced), each dibit ∈ {0,1,2,3}.
    // When the output array fills, only `step.consumed` samples were read.
    cursor += step.consumed;
    remaining -= step.consumed;
}
```

`process()` never drops an output dibit on insufficient output space. It may return `consumed < count`, so **the caller must retry the unconsumed tail**. It intentionally consumes nothing when output capacity is zero, or when a required pointer is null. It emits no dibits before sync. Call `discontinuity()` on missing IQ, retune, sample format/rate change, or receiver replacement; this drops discriminator/filter/timing/lock history and increments the discontinuity counter while retaining lifetime counters. `reset()` clears everything. Invalid non-finite/near-zero-magnitude IQ resets tracking; `stats().invalid_iq` increments. Sample indices restart after reset/discontinuity, so the application must attach its own absolute timebase.

Statistics from `stats()` include samples, symbols, lock/loss/discontinuity/invalid counts, last sync kind/correlation, estimated carrier offset and deviation gain, timing samples/symbol, relative input IQ RMS, and smoothed 4-level decision-error RMS in Hz. **Neither IQ RMS nor correlator score is calibrated signal power, measured BER, proven RF identity, or a CRC result.**

`Frontend::static_storage_bytes()` is `sizeof(Frontend)` and includes its 61-tap FIR state and 512-float history ring. CPU and memory requirements on ESP32-P4 have **NOT** been measured.

## Build and host test

From the repository root (Linux/macOS/WSL, CMake ≥3.16, C++17):

```sh
cmake -S components/orcsdr_dmr/d1_frontend -B /tmp/orcsdr-dmr-d1 -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/orcsdr-dmr-d1 --parallel
ctest --test-dir /tmp/orcsdr-dmr-d1 --output-on-failure -V
```

GCC/Clang sanitizers:

```sh
cmake -S components/orcsdr_dmr/d1_frontend -B /tmp/orcsdr-dmr-d1-sanitize \
  -DCMAKE_BUILD_TYPE=Debug -DDMR_D1_SANITIZE=ON
cmake --build /tmp/orcsdr-dmr-d1-sanitize --parallel
ctest --test-dir /tmp/orcsdr-dmr-d1-sanitize --output-on-failure
```

The single standalone CTest suite has explicit assertions active in Release (`NDEBUG`), and covers:

- All eight D0 read-only sync identifiers and ETSI nibble mapping.
- Matched SRRC synthetic 4FSK with known dibits, carrier offsets +950, −1350, +1750 and −2700 Hz, AWGN added to IQ, fractional timing shifts, ±350 ppm clock drift.
- Identical emitted dibits/sample indices across 511-sample and 1-sample IQ chunking, including single-element output buffers/backpressure.
- Reset, declared IQ discontinuity, loss/reacquisition and a new carrier after a discontinuity; invalid/non-finite data and invalid API pointers.
- Rejection of 2,500-symbol random 4FSK **without** a D0 sync, 48,000 samples of complex white noise, and 24,000 samples of an unmodulated carrier.
- Repeatable host throughput and `sizeof(Frontend)` (no waveform-generation time in the timing window).

## Recorded baseline — 2026-11-11

One GCC 14.2 `-O3 -DNDEBUG` / CMake Release run on a Linux x86-64 sandbox, generated from `tests/test_frontend.cpp` (deterministic PRNG):

| Synthetic fixture | Demodulated symbols compared | Symbol errors | SER | Estimated CFO |
|---|---:|---:|---:|---:|
| Clean, +950 Hz CFO | 1,386 | 1 | 0.00072 | +940.6 Hz |
| Shift +3.7 samples, −1,350 Hz CFO, IQ sigma .025 | 1,386 | 0 | 0 | −1,359.6 Hz |
| Shift +7.1, +1,750 Hz CFO, IQ sigma .065 | 1,386 | 0 | 0 | +1,745.4 Hz |
| +350 ppm clock, −450 Hz CFO | 3,386 | 0 | 0 | −440.6 Hz |
| −350 ppm clock, +200 Hz CFO | 3,386 | 0 | 0 | +182.6 Hz |
| Shift +5.1, −2,700 Hz CFO, IQ sigma .035 | 1,386 | 0 | 0 | −2,710.1 Hz |

All 8 sync variants: qualified; random FSK/noise/CW false-lock count: **0 in the explicitly tested finite fixtures**, not a statistical false-alarm-rate guarantee. `Frontend` object size: **2,696 bytes** on this x86-64 compiler/ABI. Throughput of repeated 15,240-sample IQ inputs, 1,524,000 samples total: **~7.9–9.0 million samples/s** in two Release runs (~164–188× the required 48,000 samples/s). Measurements exclude IQ generation, include receiver processing and output emission, and vary by host load. A sanitizer-enabled Debug run measured ~1.2 million samples/s; sanitizer performance is not a deployment benchmark. All these metrics are **synthetic host measurements, not ESP32-P4 or real RF benchmarks**.

## Safety and next gate

Keep this separate from D0 and production P25. Next D1/D2 work should supply pinned real IQ with CF32/CS16 conversion verified, quantify adjacent-channel interference, timing beyond ±350 ppm, receiver drift/clock jitter, fades, phase inversion, weak-signal false alarms over long recordings, and check real DMR sync/data burst boundaries. Only then consider burst framing, CACH, FEC, CRC and any device integration. Do **not** infer RF/site confirmation from symbol and sync statistics alone.