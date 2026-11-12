# OrcSDR DMR — D1 hardening after first verified real-air decode

**Updated:** 2026-11-11 America/Los_Angeles; recorded CI jobs are dated 2026-11-12 UTC.  
**Development branch:** `feature/dmr-digital-voice-foundation`  
**Owner of decoder changes:** lead/integrator. **Dashboard/M5GFX:** explicitly deferred.

## Established facts

- D0/D1 native host decoder has independently recovered **source ID 3024109, group 99 and color code 1** from two hash-pinned GopherTrunk direct-mode real-air IQ captures. See [first real-IQ report](REAL_IQ_PUBLIC_CAPTURE_ACCEPTANCE_2026-11-11.md) and [real-IQ CI](https://github.com/hardcoreerik/OrcSDR/actions/runs/43177469544). With one-bit sync tolerance, the first capture increased from **9 to 10** FEC-plus-integrity-valid burst events; the second retained **10**. Both were invariant across 257 and 4096 IQ samples per chunk.
- D1-P fixed-memory pre-roll reconstructs measured 54 pre-sync payload + 24 sync dibits. With the conservative D1-C bridge it preserves sample indices, reset epochs, backpressure and no synthesized input.
- D1-S now requires corroborated BS CACH cadence (or embedded-LC corroboration), rather than any Hamming-decodable TACT prefix. Direct-mode TS1/TS2 still come from their explicit sync type.
- **New D1 sync correction:** the protocol accepts a **uniquely matching 48-bit DATA sync one-bit error only after a correct Golay(20,8) slot type, complete BPTC decode and independent RS(12,9)/CRC-16 pass**. No measured dibit is rewritten. A fuzzy voice-only sync, a two-bit error, or a candidate with corrupted payload does not emit an event. The emitted D1 `Event.sync_bit_errors` records the match. Existing strict replay JSONL v1 is unchanged to retain trace comparability.
- An isolated, complete **synthetic direct-mode first burst**, previously dropped for one wrong sync bit, now produces a validated group/source LC identity in the integrated host CTest. The bridge asserts a measured pre-roll, one sync-bit error, TS1 and source/TG fields. **No verified one-burst real-air acceptance yet.**

## Exclusive next three agent scopes — planned, not yet started

| Agent | Exclusive write folder | Primary goal | Non-negotiable evidence |
|---|---|---|---|
| **D1-N — negative-control gate** | `tests/dmr_replay_acceptance/**` | Acquire or reproduce the **exact** pinned noise and AM CS16 files; run the full four-fixture D1-R harness with actual D1 binary | All four byte sizes / Git blob SHA-1 / pinned SHA-256 agree, exact ordered event hashes match chunk sizes, no invented IDs, source/host build SHA and logs saved. If private bytes unavailable: BLOCKED |
| **D1-F — independent false-sync audit** | `tests/dmr_protocol_conformance/**` | Try to defeat the new one-bit DATA sync tolerance and CACH evidence system without editing decoder source | Independently constructed 48-bit patterns, wrong sync classes, 1/2/3+ bit corruptions, corrupted payload, voice-only, random/AM-like dibits, stream resets, chunking, false identity counts, Release + ASan results |
| **D1-H — ESP32-P4 device qualification** | `tools/dmr_esp32_device_probe/**` | Opt-in, read-only IDF compile/profile harness and documented handoff into the existing Tab5 RTL-SDR receive pipeline | No second tuner/USB radio owner; bounded queues and 48 kS/s contract; measured CPU/heap/PSRAM/stack/latency/drop counters **on actual device** if available; otherwise explicit NOT RUN, no fabricated hardware benchmark |

All may read code, ETSI documents, existing tests and published source pins; agents only commit their own folder. Pull with rebase on the feature branch immediately before pushing; never merge/rebase main, rewrite branch history, force-push or modify production P25, radio owner, display/M5GFX, central firmware or other agents' files.

## Lead/integrator owns and completed first correction

- `components/orcsdr_dmr/d1_protocol/**`: error-limited verified data sync matching and independent negative/unit tests.
- `components/orcsdr_dmr/d1_integration/**`: single-burst synthetic IQ acceptance assertions, no fake sync/payload.
- `tests/dmr_protocol_conformance/**`: only immediate compatibility assertions (future agent may extend after lead stops editing this folder).
- DMR GitHub Actions: now also runs the native D1 protocol source-linked unit/CACH tests in Release and sanitizer alongside independent conformance.

**Required lead acceptance after new agents:** rerun strict native protocol, frontend and integration CI (Release + sanitizers), the public real-air CI and the full four-fixture D1-R gate when source-pinned bytes become available. Compare prior and new *integrity-checked* burst counts and false identity rate, not only sync counts. Retune/memory work remains staged; P25 runtime must not be altered until safe integration approval.

## Hold points

1. **Full D1-R gate:** two public real-air positive recordings are verified/decoded; the **exact** two private synthetic noise and AM controls have not yet run through the native bridge. No unpinned surrogate can close this gate.
2. **Sync false positives:** one-bit candidate must have unique data-sync class and valid independent integrity. Still needs longer-duration independent stress/fuzzing before declaring acceptable field false-alarm rates. Two-bit sync correction is **not** implemented.
3. **Actual ESP32-P4:** host tests and host synthetic throughput do **not** prove on-device CPU/RAM/stack headroom, USB IQ continuity or SD write isolation. Keep this gate open until hardware measurements.
4. **Broader protocol/corpus:** Tier III control and voice terminator recordings are pinned but not yet proven to decode; Tier III trunking is not a D1 promise.
5. **M5GFX/dashboard:** deliberately postponed until live data contracts and device measurements are stable.

## Repeatable lead verification

```bash
cmake -S components/orcsdr_dmr/d1_integration -B build/dmr-d1 -DCMAKE_BUILD_TYPE=Release
cmake --build build/dmr-d1 --parallel
ctest --test-dir build/dmr-d1 --output-on-failure

cmake -S components/orcsdr_dmr/d1_protocol -B build/dmr-protocol -DCMAKE_BUILD_TYPE=Release
cmake --build build/dmr-protocol --parallel
ctest --test-dir build/dmr-protocol --output-on-failure

cmake -S tests/dmr_protocol_conformance -B build/dmr-independent -DCMAKE_BUILD_TYPE=Release
cmake --build build/dmr-independent --parallel
ctest --test-dir build/dmr-independent --output-on-failure
```

CI repetitions must additionally run AddressSanitizer/UndefinedBehaviorSanitizer. The public replay workflow downloads real IQ to temporary runner storage and uploads only JSON evidence; private IQ must never enter OrcSDR source control.
