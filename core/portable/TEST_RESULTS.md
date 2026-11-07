# Portable core: verified build/test evidence and technical handoff

**Evidence date:** 2026-11-06 Pacific / 2026-11-07 UTC.
**Source commit tested:** `531c7ce543b1370209be609f13b597c0d27db936`
**Workflow run:** [42321833790](https://github.com/hardcoreerik/OrcSDR/actions/runs/42321833790)
**Environment:** GitHub Actions `ubuntu-24.04`, Linux x86-64, GNU/Clang native C++17, CMake, CTest.
No Android NDK, QEMU, Emscripten, ESP-IDF or SDR hardware used in these jobs.

## Observed automated results

| Runner/compiler configuration | Compile of original ACARS + ADS-B | Git blob verifier | ACARS smoke | ADS-B self-check |
| --- | --- | --- | --- | --- |
| Linux x86-64 / GCC / Debug | PASS | PASS | PASS | PASS |
| Linux x86-64 / GCC / Release | PASS | PASS | PASS | PASS |
| Linux x86-64 / Clang / Debug | PASS | PASS | PASS | PASS |
| Linux x86-64 / Clang / Release | PASS | PASS | PASS | PASS |

**Observed:** 4/4 completed GitHub Actions jobs succeeded; 8/8 CTest
invocations passed (two tests in each configuration). The verifier output
reported `Canonical: 73 Headless-mapped files + 5 Mobile mappings checked`
and `PASS: Git blob hashes match recorded source snapshots` on the OrcSDR
tree. That verifier checks only **canonical OrcSDR files** unless actual
Headless/Mobile checkout paths are supplied. Headless/Mobile tree comparison
itself was performed read-only using the remote Git blob SHAs at the pinned
refs in the inventory; it was not a reference checkout in CI.

Example recorded CTest output, Clang Release job:
```text
Start 1: portable_acars_smoke
1/2 Test #1: portable_acars_smoke .............   Passed
Start 2: portable_adsb_selfcheck
2/2 Test #2: portable_adsb_selfcheck ..........   Passed
100% tests passed, 0 tests failed out of 2
```

The existing ACARS smoke source was updated to use a fatal `REQUIRE` check
instead of `assert`, so a Release configuration with `NDEBUG` still executes
the complete CRC, inverted-bit, single-bit-correction and synchronization
checks. The separate ADS-B test exercises the unchanged production
`Decoder::self_check()` deterministic fixture set. Neither of these is
an over-the-air acceptance test.

## Drift and integration warnings

- **Headless source copies:** 71 of 73 vendor files match OrcSDR by Git blob
  SHA-1. `lora_native_decoder.cpp` and `.hpp` differ in a material way.
  Current OrcSDR has the `workspace_failed` status, capture-workspace
  lifecycle helpers, `compact_preprocess_capture` and
  `decode_capture_in_place`. Headless's older source instead retains a
  filter + linear-resample capture path. This must be reconciled with tests;
  **do not back-port the older file over main**.
- **Mobile source copies:** `acars_core.{cpp,hpp}` match byte for byte.
  `adsb_decoder.{cpp,hpp}` are otherwise byte-identical after removing their
  one-line added SPDX comment (verified by comparing complete source text).
  The extracted `frontend_coeffs.hpp` also has the same Git blob hash.
- **Headless adapter maturity:** `orcdec` C ABI, per-decoder adapters, host,
  WASM and ESP-IDF builds all exist in the inspected branch. Its
  `docs/ui/PHASE3_DECODERS.md` describes several decoders as absent; that
  earlier phase document is stale relative to the **current** `sources.sh`
  and the tree. Prior phase test claims cannot be promoted to current full
  eight-decoder parity without rerunning the harness.
- **Mobile DSP:** the ACARS/ADS-B port uses existing decoding sources, while
  Mobile's channelizer, DSP and C ABI adapter code are separate adaptations;
  compile success is not evidence that both platforms process IQ identically.

## Reproduction and next agents

Start from the implementation branch and use:
```sh
cmake -S core/portable -B build/portable -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build/portable --parallel
ctest --test-dir build/portable --output-on-failure
python3 core/portable/check_source_inventory.py
```

For cross-repository source verification, check out the **specific recorded
commits** and supply `--headless` and `--mobile` paths to the verifier.
If those refs advance, first compare blob hashes and update the manifest
with a reviewed reconciliation; do not silently change baseline pins.

Other agents should consume `orcsdr_core_acars`, `orcsdr_core_adsb` and the
machine-readable `source_inventory.json` as initial source-backed contracts.
The Web/`orcdec` ABI is a **candidate** for shared usage; Mobile's own native
ABI remains independent until an explicit ABI/ownership compatibility review.
Neither repo was edited. No production ESP32-P4 files or build registration
were edited. This branch must **not** be automatically merged to `main`.

**Not run / not claimed:** native Linux ARM64, Android ARM64, x86-64 Windows,
WebAssembly, P4 firmware rebuild, P4 hardware/runtime/SDR reception, Headless
host-vs-WASM-vs-device parity, Mobile UI/JNI tests, broad FT8/JS8/P25/POCSAG/
SSTV/LoRa portability, or commercial licensing approval.
