# P1 Remote ID decoder + observation store — host checkpoint

> **Date:** 2026-10-28 America/Los_Angeles (CI run 2026-10-29 UTC).  
> **Branch:** `chatgpt/drone-watch-phasing-20261026` — reused by explicit owner instruction, no new OrcSDR branch.  
> **Evidence grade:** C/C++ **host-build + deterministic tests PASS**. **Native ESP-IDF, physical Tab5, actual BLE/Wi-Fi Remote ID capture, and end-to-end W01 live feed: NOT RUN.** AddressSanitizer/UndefinedBehaviorSanitizer host suites: **PASS**.  
> **CI:** [GitHub Actions P1/W01 host run — 3/3 PASS](https://github.com/hardcoreerik/OrcSDR/actions/runs/40827333255).

## Delivered source

- `apps/orcsdr-tab5/ui/drone_watch_rid.{hpp,cpp}` — OrcSDR bounded, caller-supplied **25-byte ASTM/OpenDroneID application-payload** decoder; pack `3 + 25*N` bytes, `1 <= N <= 9`; version <=2; strict length/type checks; byte-copy before vendor bitfield decoding; all-or-nothing batch exposure; no heap allocation.
- `apps/orcsdr-tab5/ui/drone_watch_rid_store.{hpp,cpp}` — fixed 32-event observation metadata ring, local monotonic sequence, capture source ID/transport/time, replay flag and history overwrite counters. **Not a multi-source deduplicating aircraft tracker.**
- `apps/orcsdr-tab5/ui/third_party/opendroneid/opendroneid.{c,h}` — verbatim official core at `849c03adbe0cdd68997a462fafab397edcfe53f3`, with `LICENSE` (Apache-2.0) and `UPSTREAM.md`.
- `apps/orcsdr-tab5/tests/drone_watch/{rid_test.cpp,store_test.cpp}`, host `CMakeLists.txt`, native `main/CMakeLists.txt` — compile and model tests.

Exact Git blob content-hash comparisons against upstream pinned revision:

| File | Pinned Git blob SHA | Result |
| --- | --- | --- |
| `opendroneid.h` | `0c11f57923e4861ca7b171c87aaf29bfed9183a3` | IDENTICAL |
| `opendroneid.c` | `13f7e2bb9cc036f09b88d3c31bd0f489f3862692` | IDENTICAL |
| `LICENSE` | `261eeb9e9f8b2b4b0d119366dda99c6fd7d35c64` | IDENTICAL |

## Actual executed CI check

The existing `drone-watch-w01-host.yml` workflow now builds the actual vendor C core, bounded C++ wrapper, source-aware ring and W01 renderer/model. Ubuntu CI ran the following both normally and with `DRONE_WATCH_SANITIZE=ON` (`-fsanitize=address,undefined -fno-omit-frame-pointer`):

```text
cmake -S apps/orcsdr-tab5/tests/drone_watch -B build-drone-watch-tests
cmake --build build-drone-watch-tests --parallel
ctest --test-dir build-drone-watch-tests --output-on-failure

1/3 Test #1: drone_watch_host ... Passed
2/3 Test #2: drone_watch_rid  ... Passed
3/3 Test #3: drone_watch_store ... Passed
100% tests passed, 0 tests failed out of 3
```

C++17 test targets use `-Wall -Wextra -Wpedantic -Werror`; the unmodified upstream C code retains its own original compilation mode. The `rid_test` includes an independently constructed BasicID/Location little-endian payload fixture, differential upstream BasicID encoding, a valid two-message pack, invalid lengths/types/versions, missing/invalid coordinates and speed, atomic reject, and **5,000 deterministic fuzz candidates** with a fixed seed. These **are not** coverage-guided fuzzing, but the same deterministic test binaries have now also run successfully under ASan/UBSan.

The store test uses **two independently labeled logical source IDs**, fixture replay marker, invalid frame rejection, 32-entry wrap and 11 overwritten event counts. This is **not** two physical OrcNodes, nor real BLE/Wi-Fi capture.

## Reference format gotcha found during test

In the pinned OpenDroneID core, horizontal speed 0xFF has different meaning depending on the location header's speed-multiplier bit:
- multiplier=0: `255 * 0.25 = 63.75 m/s` is a valid value;
- multiplier=1: `(255*0.75 + 255*0.25) = 255 m/s` is the invalid/unknown sentinel.

An initial host test incorrectly expected 0xFF alone to be invalid. It failed; the fixture was corrected to set the multiplier bit and the final suite passed. Do not propagate raw sentinel checks without reproducing the actual decoder result.

## Provenance and safety rules

- `CaptureMeta` timestamps are **local monotonic capture callback time**, never decoded UTC. Source ID must originate from a future verified acquisition adapter; it is not cryptographically authenticated.
- Parsed `BasicID` serial and registration identifiers are **broadcast claims**. Binary session IDs are not silently printed as ASCII.
- Valid Location lat/lon is **unverified broadcast-reported location**. The standard's `(0,0)` location sentinel is not plotted; invalid/out-of-range coordinates are withheld. Altitude/heading/speed separately carry validity flags and units.
- `Auth` data present does not imply verified signature. Operator/System operator data is deliberately not included in the normalized public UI payload.
- The parser consumes **already extracted OpenDroneID payloads**, not raw BLE advertising TLVs, Wi-Fi Beacon IEs, NAN frames or IQ. C6 capture, 802.11 transport extraction, receiver coexistence, OrcNode connection and real-air validation remain independent work.

## Reproduce locally (save log alongside command)

From the OrcSDR checkout root, on this same branch:

```powershell
git fetch origin
git switch chatgpt/drone-watch-phasing-20261026
git pull --ff-only
cmake -S apps/orcsdr-tab5/tests/drone_watch -B build-drone-watch-tests
cmake --build build-drone-watch-tests --config Debug
ctest --test-dir build-drone-watch-tests -C Debug --output-on-failure *>&1 |
    Tee-Object -FilePath .\drone-watch-host.log
if ($LASTEXITCODE -ne 0) { throw "Drone Watch host tests failed" }
```

`drone-watch-host.log` is stored in the current working folder. Follow the repository's normal `apps/orcsdr-tab5/tools/build-tab5-idf.ps1` only in a correctly pinned ESP-IDF environment. Do not flash/modify C6 firmware without explicit approval. A native build **has not been run in this sandbox**.

## Next gates and known scope gaps

1. Extend complete conformance: reference-generated tests now cover System/SelfID/Auth/OperatorID, and singleton constraints reject duplicate Location in a pack. Continue with more protocol/transport edge cases and clock/source grouping; ASan/UBSan host runs are PASS, not a substitute for physical RF. Do not imply full ASTM certification.
2. Run actual **native ESP-IDF 5.5.4 build** on the normal worktree; record app size, free heap/PSRAM and any C compiler issues; confirm W01 still draws.
3. P2: prove whether Tab5's onboard ESP32-C6 ESP-Hosted firmware exposes passive BLE 4/5 Remote ID frames; explicitly separate Wi-Fi Beacon/NAN feasibility and preserve Wi-Fi scan/catalog and RTL continuity. No C6 firmware flash without separate owner approval.
4. P4: only after a trusted raw-advertising input adapter and real reference capture exist, feed validated source model to W01 and progressively A01/A02/A03. `SOURCE UNAVAILABLE` remains the truthful runtime state until then.
5. Perform real hardware and network/SD/radio regression before changing product status. Keep REA-Results findings in its `inbox/` per the archive instructions.

**This checkpoint is a build/test milestone for the software decoder, not live drone detection.**

## Follow-up evidence update (same date)

[Final ASan/UBSan GitHub Actions run](https://github.com/hardcoreerik/OrcSDR/actions/runs/40827333255): normal CTest 3/3 PASS and sanitizer-instrumented CTest 3/3 PASS. The expanded suite now checks all six individual OpenDroneID message families (the extra four via official pinned reference encoders), duplicate singleton message types in packs, and the original fixed-seed 5,000 random/invalid payloads. No native ESP-IDF compiler, hardware or passive RF decoder pipeline was exercised.
