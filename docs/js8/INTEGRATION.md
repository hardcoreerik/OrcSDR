# JS8 decoder integration contract

Status: design only. No firmware binding in this workstream.

## Ownership

The JS8 decoder lives under `apps/orcsdr-tab5/ui/js8_*.{hpp,cpp}` in namespace `orcsdr::js8`. It must not change FT8/FT4 decoder behavior.

## Future backend shape

The eventual backend mirrors the native FT8/FT4 lifecycle:

```cpp
begin(12000, submode, Config, Memory)
begin_slot(epoch_ms)
offer_audio(samples, count)
finish_slot(orcsdr::ft8::Decode* out, capacity)
stats()
reset()
```

Large buffers come from caller-provided `Memory{alloc, release}`. Runtime decode paths must not allocate from the ordinary heap.

The backend is intentionally not added in slice 1 because FEC/CRC acceptance is not yet reconstructable. A stub returning zero would create a misleading appearance of decoder readiness.

## Shared-code boundary

JS8 may consume established project interfaces and data types, but this workstream does not alter:
- `ft8_runtime.*`
- `ft8_native_backend.*`
- `ft8_dashboard.*`
- `ft8_model.*`
- `main.cpp`
- shared FT8/FT4 DSP/FEC behavior.

If reuse later requires a shared-code change, document the exact proposal here before editing it.

## Output truth rule

A JS8 result will populate `orcsdr::ft8::Decode` only after:
1. credible JS8 sync;
2. verified tone-to-bit mapping;
3. reconstructed FEC success;
4. reconstructed CRC success;
5. supported frame parsing.

Multi-frame JS8 text needs separate assembly state; individual raw frames must not masquerade as complete conversations.

## Next integration work

After reconstruction fixtures arrive:
- add Normal 12 kHz spectral candidate extraction;
- recover tone labeling;
- add generated JS8 FEC graph from reconstruction evidence;
- add CRC implementation from reconstruction evidence;
- add SNR estimator and synthetic calibration;
- then add the backend lifecycle.

No CMake/firmware source-list change is justified until a real decoder source file needs device compilation.


## Candidate demod primitive

`js8_demod.*` is allocation-free and float-only in its hot loop. It evaluates a caller-supplied time/frequency candidate and returns raw tone decisions; it does not search the whole passband or produce `Decode`.

The current exact single-tone correlations are a correctness/reference primitive. Before device binding, a bounded coarse-search front end should produce candidates and this stage, or an optimized equivalent, should refine only those candidates. No ESP32-P4 timing claim is made from the host unit test.


## Sync candidate search

`js8_sync.*` consumes a caller-owned non-negative energy grid and returns bounded caller-owned candidates. It allocates no memory and knows only JS8 profile geometry. That keeps future spectral generation replaceable: the P4 backend can reuse an existing FFT primitive without changing JS8 sync logic.

The search result must never be surfaced as a decode. Only the later FEC + CRC + frame parser may create `Decode`.


## Spectral reference front end

`js8_spectral.*` establishes the numerical reference for PCM -> energy-grid
conversion without changing the shared FT8/FT4 spectral code. It uses only
single-precision arithmetic in the hot correlation loop and writes exclusively
to caller-owned buffers.

For ESP32-P4 production, do not run a dense 200-3000 Hz exact-correlation grid
unless measurement proves it fits the slot budget. The preferred integration is
a bounded FFT/coarse-search implementation, compared against this JS8 oracle on
identical PCM fixtures before it is accepted.


## Front-end orchestration

`js8_frontend.*` is the device-shaped pre-FEC orchestration layer. It owns no
large buffers and performs no heap allocation. The future backend supplies:

- the slot PCM;
- a spectral energy grid;
- a bounded output array.

It returns only raw JS8 candidates. That separation is deliberate: firmware
must not surface sync/tone evidence as a decoded message.

The eventual native backend should replace the exact-correlation grid builder
with a measured FFT/coarse-search implementation, then feed its grid into this
same front-end contract. Once FEC/CRC are reconstructed, the acceptance layer
can be appended without changing the front-end API.


## Generic FEC engine

`js8_fec.*` is ready to consume the reconstructed sparse parity graph. It is
not tied to a hard-coded JS8 matrix:

- graph adjacency is supplied as immutable arrays;
- workspace size is derived from the graph;
- workspace storage is caller-owned;
- the hot loop uses single-precision float;
- no heap allocation occurs in `decode()`;
- convergence means parity only, never message acceptance.

After the reconstruction corpus yields a complete sparse graph, a generated
static graph header can bind directly to this engine. CRC and frame parsing
remain downstream mandatory gates.


## JS8 SNR

`js8_snr.*` follows the OrcSDR weak-signal convention: signal-to-noise power
is normalized to a 2500 Hz reference bandwidth. It consumes the validated raw
tone sequence plus a spectral grid and uses only single-precision arithmetic in
the estimator.

The current calibration offset is exactly 0.0 dB because the deterministic
synthetic plain-FSK sweep shows less than 0.05 dB mean bias from +8 through
-12 dB. That is a host-fixture result only. Firmware integration must retain an
SNR-unavailable flag until real JS8 reference pairs establish that the estimator
is calibrated on actual JS8 waveforms.


## Firmware integration (Tab5) - measured 2026-10-08

Status: the receive-only JS8 Normal front end is bound into the Tab5 firmware. It produces raw sync/tone evidence only. **No
`Decode` record is ever produced**: the tone-to-bit map, FEC graph, CRC and frame parser are not accepted, and `finish_slot()` returns 0.

### What is bound
- `js8_native_backend.{hpp,cpp}` (namespace `orcsdr::js8::native`): `begin(12000, submode, Config, Memory)`, `begin_slot`, `offer_audio`,
  `finish_slot`, `stats`, `reset`, plus read-only diagnostics (`raw()`, `candidate()`, `grid()`). Only Normal is accepted; `begin()` and
  `set_submode()` refuse Fast, Turbo, Slow and the experimental mode and never fall back to Normal.
- Spectral stage: one 1920-point FFT per half symbol using the existing mixed-radix FFT primitive, 449 bins on the 6.25 Hz tone spacing
  (200 to 3000 Hz). It agrees with the exact-correlation oracle `js8_spectral` to 1.65e-5 of the strongest cell on identical PCM
  (`tests/js8_native_backend_tests.cpp`, also run under ASan/UBSan).
- Runtime (`ft8_runtime.cpp`): the JS8 backend is allocated lazily in PSRAM the first time a JS8 mode is used (about 700 KB), so FT8/FT4
  users do not pay for it. It reuses the existing 12 kS/s audio tap and ring; there is no second IQ front end. FT8/FT4 and JS8 are never run
  at the same time. The runtime refuses every JS8 submode except Normal.
- JS8 calling frequencies (one dial for every submode): 160 m 1.842, 80 m 3.578, 40 m 7.078, 30 m 10.130, 20 m 14.078, 17 m 18.104,
  15 m 21.078, 12 m 24.922, 10 m 28.078, 6 m 50.318 MHz. 60 m and 2 m keep the FT8 dial until confirmed against public JS8 documentation.
- Serial (receive only): `JS8 STATUS | STATS | RAW | START | STOP | MODE NORMAL | BAND <label> | INJECT BEGIN|PING|RUN|<offset> <b64>`.
  `JS8 RAW` prints measured evidence (audio Hz, time offset, sync score, sync hits, margin) and never text.
- Injection: `python tools/tab5_ft8.py --port COM17 inject <12 kHz mono wav> JS8` runs a recording through the real backend on the Tab5.

### Tab5 measurements (ESP32-P4, 360 MHz, one 15 s slot, nothing extrapolated from the host)
Test recordings: synthetic Normal slot, tone 0 at 1012.5 Hz, signal amplitude 6000 counts with uniform noise of 2500 counts, frame 0.5 s
into the slot, generated with numpy from the Normal sync/data tone sequence used by `tests/js8_demod_tests.cpp`; and a noise-only slot with
the same noise amplitude. Uploaded over serial with CRC verification, then run with `inject ... JS8`.

| Recording | Raw frames | Decodes | Best sync | Spectral | Sync search | Demod | Total |
|---|---:|---:|---:|---:|---:|---:|---:|
| Synthetic signal (1012.5 Hz, dt -20 ms, sync hits 21) | 1 | 0 | 0.97 | 326 ms | 111 ms | 834 ms | 1271 ms |
| Noise only | 0 | 0 | 0.26 | 325 ms | 110 ms | 834 ms | 1269 ms |

JS8 Normal uses about 8.5 percent of a slot (FT8 uses about 3.5 s). Firmware size: the app is 4,072,672 bytes, +14,080 bytes over the
build without JS8 (4,058,592), in the 6 MB app partition. No dropped analysis blocks were seen; FT8 and FT4 host results are unchanged.

### Memory finding that cost a boot loop
The first flash of the runtime hook boot-looped (`Could not reserve internal/DMA pool (error 0x101)`, then `abort()`): an 8 KB static
chunk buffer in `decode_slot` pushed the static internal RAM past what the 40 KB boot-time DMA reserve allows. The JS8 path now shares the
existing buffer, and the map's 16 KB of projection arrays moved to PSRAM. After the fix the largest internal DMA block after boot is
38 KB (52 KB free). Rule for later work: do not add static internal RAM without checking `RTL_DRAM_BUDGET`.

### Real-capture front-end evidence (host, same code as the firmware)
Dataset: `F:\AI\OrcSDR-TEMP\js8-pc-capture` commit `1705472`, `tests/fixtures/js8/corpus/2026-10-08`, verified with
`tools/js8_capture/verify_dataset.py` (7 IQ/WAV pairs, 870 standard-decoder runs, 5 unique frames).
Capture `sample-40m-180s-002` (RF dial 7.078 MHz, IQ centre 7.126 MHz, gain 40.2 dB, 180 s): raw IQ SHA-256
`6eff2c58b939422023fa69c0a7e2b3180f662767dacf451136e72aa834aca83f`, 12 kHz WAV SHA-256
`93b48bef6a6799e0301d1459ee724759772f5e0ff8e83249337a82747c44a445`. No AGC and no normalisation were applied.
Method: `tools/js8-wav-front.cpp` slides a 15 s window over the WAV in 1 s steps (166 windows), runs the backend, and marks any raw frame
within 7 Hz of a frequency the standard decoders reported (JS8Call CLI 2.2.0 and GUI 3.0.3). Results are bound to the hashes and the decoder
commit in `docs/js8/results/2026-10-08-sample-40m-180s-002-front-end.json`.

| Reference frame (standard decoders) | Audio Hz | Result in OrcSDR front end |
|---|---:|---|
| WO7I HEARTBEAT, reference SNR -12 dB | 635 | found in 7 windows, best sync 0.534 at 637.5 Hz |
| K8IMT HEARTBEAT, -23 dB | 486 | found in 5 windows, best sync 0.519 at 487.5 Hz |
| K7YXZ HEARTBEAT, -18 dB | 838 | found in 4 windows, best sync 0.292 at 837.5 Hz |
| KD7WPQ HEARTBEAT, -20 dB | 2604 | not found: sync candidates at 2600-2606 Hz (0.12 to 0.19) fail the demodulator's sync-hit check |

16 raw frames in total, all near a reference frequency (no unexplained extras). Zero-decode controls `sample-20m-180s-001`,
`sample-40m-180s-001`, `probe-20m-001`, `probe-40m-001` and the Slow capture `probe-20m-002` give 0 raw frames in the front end; the
reference result for those files is not treated as proof of silence. The first failing stage for the 2604 Hz frame is the sync-candidate
to demodulation threshold; downstream stages (tone mapping, FEC, text) are the reconstruction workstream's and are not attempted here.
