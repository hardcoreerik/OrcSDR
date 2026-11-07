# Canonical C++ radio core: phase 0 portability foundation

**Status:** source inventory and initial native-Linux CMake gate. This is NOT an
ESP32 firmware migration, ABI release, Android build, WebAssembly build or RF
acceptance. Existing production code and the Headless/Mobile repositories stay
unchanged.

## Source of truth

OrcSDR `apps/orcsdr-tab5/ui/` is the current authoritative implementation
for the decoder files inventoried in `source_inventory.json`. Until a coordinated,
separately tested move, **compile these files from this directory** rather than
forking a portable copy. The `core/portable/CMakeLists.txt` does precisely that.

Pinned Git trees inspected on 2026-11-06:

| Repository and ref | Exact commit |
| --- | --- |
| OrcSDR `main` | `1dbf2d455a5c2bf8bf69ef804336418879b7325f` |
| OrcSDR-Headless `feat/m5gfx-web-mirror` | `67e535e3a8562709d190194363f7d81ce77fe2f3` |
| OrcSDR-Mobile `main` | `b5fe05d39972c0ee4b1cb6830f903facf58cb3b2` |

The 73 files copied to Headless's `core/decoders/vendor/orcsdr/` have
**71 identical blob hashes** to the corresponding OrcSDR files. The two
exceptions are `lora_native_decoder.cpp` and `lora_native_decoder.hpp`;
neither may silently replace the other. This comparison is **source parity**,
not proof of identical behavior. The Headless adapter files `orcdec_*.cpp`,
`orcdec.cpp`, `orcdec_internal.hpp`, `include/orcdec.h`, shims, source list,
host and WASM build scripts, ESP-IDF component, and
`tests/decoders/parity.py` are valuable integration references, not
canonical decoder algorithm implementations.

Mobile's extracted `acars_core.{cpp,hpp}` blobs match the current
authoritative originals exactly. Mobile's `adsb_decoder.{cpp,hpp}` have
different blob hashes **only because a SPDX comment was added**; the Mobile
manifest and `native/core/adsb/UPSTREAM.md` document that one-line adaptation.
Its `dsp/upstream/frontend_coeffs.hpp` is byte-identical to the Tab5
coefficient table. Mobile's DSP chain, C ABI wrappers, and scanner ports
contain platform-owned/adapted code; they are not automatically replacements
for the Tab5 signal-processing pipeline.

All hashes and file paths are machine-readable in `source_inventory.json`.
The portable build only depends on the standard C++17 library; the firmware
build and its component manifests are untouched.

## Initial host targets

| CMake target | Compiled canonical source | Gate |
| --- | --- | --- |
| `orcsdr_core_acars` | `ui/acars_core.cpp` | Existing `tests/acars_core_smoke.cpp`, active checks even with `NDEBUG` |
| `orcsdr_core_adsb` | `ui/adsb_decoder.cpp` | Calls the production decoder's `Decoder::self_check()` fixture suite |

```sh
cmake -S core/portable -B build/portable -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build/portable --parallel
ctest --test-dir build/portable --output-on-failure --no-tests=error
python3 core/portable/check_source_inventory.py
```

To check actual reference checkouts at the **pinned commits**, without copying
them into OrcSDR:

```sh
python3 core/portable/check_source_inventory.py \
  --headless ../OrcSDR-Headless --mobile ../OrcSDR-Mobile
```

The default hash gate checks the canonical tree only, because private
reference repositories cannot be assumed to be available to GitHub Actions.
Supplying reference checkouts verifies their pinned copied files; it does **not**
execute Headless host/WASM/device parity, which still needs its own harness.

CI: `.github/workflows/portable-core.yml` executes the hash gate, compile
and CTest across GCC/Clang and Debug/Release on Linux x86-64. CI is not ARM64,
Android, ESP-IDF or WASM evidence.

## Full decoder triage / next extraction gates

| Decoder or DSP area | Canonical location | Current boundary and work |
| --- | --- | --- |
| VHF ACARS | `ui/acars_core.*` | Pure C++17; initial native build target; IQ adapter remains outside the core |
| ADS-B | `ui/adsb_decoder.*` | Pure C++17, fixed 2.048 MS/s expectation; initial native build target |
| FT8 / FT4 | `ui/ft8_{native_backend,demod,pipeline,sync,ldpc*,codec,message,...}` | Broad DSP graph already built by Headless via source list; require reproducible golden fixtures and precise CMake dependency closure |
| JS8 | `ui/js8_{native_backend,decoder,demod,fec,frontend,...}` | Same; retain measured Normal-mode limits; no blanket all-speed claims |
| P25 | `ui/p25_decoder_core.*` | Core signal path distinct from platform-specific voice, IMBE/mbelib and UI integration |
| POCSAG | `ui/pocsag_decoder_core.*` | Native allocation uses `ESP_PLATFORM` conditional `esp_heap_caps`; verify host and ESP memory policies independently |
| SSTV | `ui/sstv_{receiver,modes,auto_detect}.*` | Host-testable with conditional ESP heap/timer paths; color and mode fixture parity remains separate |
| LoRa / Meshtastic | `ui/lora_{native_decoder,preamble_detector}.*` | ESP-DSP FFT, ESP timer/heap, FreeRTOS, AES dependencies; Headless uses shims; source drift needs reconciliation first |
| VDL2 | `ui/vdl2_*.{cpp,hpp}` | Integrated in main ACARS dashboard but absent from current Headless `orcdec` vendor list; inventory for next iteration |
| RF analysis / multirate DSP | `ui/rf_analysis.*`, `dsp/*`, `ui/main.cpp` | M5Unified/ESP-DSP/RTOS and large stateful production audio path; isolate algorithm vs device/audio/UI before promising portable reuse |

This is a **bounded build proof**, not a decision to relocate every decoder.
P25 Phase II, FT4, JS8 variants, LoRa encrypted payloads and all other
protocol limits retain the current `PROJECT_STATUS.md` evidence boundaries.

## Proposed end-state (not implemented here)

1. Preserve exactly one canonical algorithm implementation per decoder, ideally
   a source-owned `core/radio/` layout *after* firmware build-path changes are
   independently tested. Avoid parallel hand-maintained `vendor/` forks.
2. Build targets expose stable C++17 internals with explicit sample formats,
   sample rates, ownership, timestamps, reset/discontinuity semantics and
   revision/provenance. Drivers, displays, UI, storage and platform scheduling
   live behind adapters.
3. Keep `orcdec` as the leading C ABI **candidate** for Linux, Android and
   WASM, preserving its existing C functions and event schema until an ABI and
   decoder-lifetime review. Do not prematurely choose its event JSON as the
   production firmware API or silently replace Mobile's current C ABI.
4. Use distinct host, ESP-IDF, Android/NDK and Emscripten build frontends that
   compile the **same source files**; cross-compiler golden-vector tests plus
   chunking, reset, and event/attachment parity gates must pass before each
   platform is marked supported.
5. Resolve third-party and commercial-rights provenance before distributing
   any proprietary binaries. OrcSDR defaults to AGPL-3.0-only with separate
   commercial terms; P25 mbelib has independent upstream notices.

## Handoff / evidence gate

The commit introducing this directory is the initial shared-source CMake
foundation. Other agents should reference the exact Git commit, consume
`core/portable/source_inventory.json` and the `orcsdr_core_acars` /
`orcsdr_core_adsb` targets, and keep independent ports in reference repos
until coordinated migration. An agent must not import the Headless LoRa copy
or Mobile ADS-B copy over main merely because its API looks compatible.
Recorded **actual** build results belong in the handoff report and GitHub
Actions job logs, never inferred from the presence of this workflow.
