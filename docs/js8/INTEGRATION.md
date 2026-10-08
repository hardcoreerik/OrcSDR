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
