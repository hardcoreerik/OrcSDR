# FT8 companion validation — 2026-10-30

Host-verified development integration. No hardware access or flash during this
continuation. No merge into main or release publication.

## Source and artifacts

| Artifact | Clean source commit | Bytes | SHA-256 |
|---|---|---:|---|
| Tab5 application | `8c1eb23a55bd5568e59bf0780ffbbb37bf703b68` | 5439024 | `0eb38cbbc11f122af96b9b92c2551a700d18bf84033bca1ac7784f0d5018c3b4` |
| Dial application | `ca2a25b3b67a49843bbe17f105e937f942247e50` | 1136064 | `0d3ab48fbad89533eeac0009562ca6c9046df9d72fd082a24b67b93c61c86cc2` |

The Tab5 branch incorporates upstream main `d353f8dbc6d2f1a76e1cea4ad13e164fce1774d9`
through merge commit `6a5be4f8`; no shared history was rewritten. The Dial branch
remains stacked on the planning branch for PR #13. Later documentation-only
commits do not change the source revisions of these saved artifacts.

Local copies and JSON hashes:

- `F:\AI\OrcSDR-TEMP\orcdial-ft8-integration\.pio\ft8-integration\firmware-8c1eb23a.bin`
- `F:\Ai\OrcDial-local-work\slice1-validation\.pio\hardware-slice1\firmware-ca2a25b.bin`
- `firmware-evidence.json` in each of those local working test directories.

## Commands and results

Run in WSL Ubuntu with g++ 13.3.0, from the appropriate repository unless stated.

```bash
# OrcSDR: PASS, including optimized + ASan/UBSan suites and decoder checks.
bash tools/test-ft8.sh

# OrcDial: PASS, all 9 tests; uses the actual OrcSDR encoder in security_test.
cmake -S tests -B .pio/ft8-host -DCMAKE_BUILD_TYPE=Debug \
  -DMBEDTLS_SOURCE_DIR=/mnt/c/Espressif/frameworks/esp-idf-v5.5.4/components/mbedtls/mbedtls \
  -DORCSDR_SOURCE_DIR=/mnt/f/AI/OrcSDR-TEMP/orcdial-ft8-integration
cmake --build .pio/ft8-host -j 2
ctest --test-dir .pio/ft8-host --output-on-failure

# OrcDial: PASS, additional focused cache/negotiation sanitizer check.
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-omit-frame-pointer tests/projection_delivery_test.cpp \
  -o .pio/orcdial2-host/projection_delivery_sanitized
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  .pio/orcdial2-host/projection_delivery_sanitized
```

PowerShell firmware commands, no upload targets:

```powershell
# OrcDial: PASS, pinned production environment, Python 3.11.7.
& C:\Users\hardc\.platformio\penv\Scripts\python.exe -m platformio run -e dial

# OrcSDR: PASS, pinned ESP-IDF 5.5.4, normal pinned C6 image unchanged.
& ./apps/orcsdr-tab5/tools/build-tab5-idf.ps1

# Final clean-source incremental build in that configured IDF environment: PASS.
$env:IDF_PYTHON_ENV_PATH = 'C:\Espressif\python_env\idf5.5_py3.14_env'
$env:PATH = "$env:IDF_PYTHON_ENV_PATH\Scripts;$env:PATH"
. C:\Espressif\frameworks\esp-idf-v5.5.4\export.ps1
ninja -C apps/orcsdr-tab5/build-native-hosted3 -j 2
```

The native application fits its 6 MiB partition with 14% free; the build's
existing check confirms the shared 8192-byte FT8/FT4/JS8 PCM scratch is in PSRAM.
Target ELF symbols measure the new latest-decode producer at 116 bytes and peer
negotiation at 16 bytes, plus a 4-byte generation cursor. No new PSRAM/DMA buffers
or tasks were introduced. Dial static RAM is 49380/327680 bytes (15.1%). These are
build facts, not runtime memory-headroom or radio-continuity measurements.

The first host build encountered the existing missing `esp_heap_caps.h` stub;
the five-line test-only fix from OrcDial PR #7 was included as separate commit
`dfa035f`. Initial environment/runner failures were corrected; the logs named
below are the authoritative final passes. No full release-check/capture run,
standalone host Release configuration, or physical test is claimed.

## Saved local evidence

Under the OrcSDR working test directory `.pio/ft8-integration/`:
`final-ft8.log`, `tab5-build.log`, `final-tab5-build.log`,
`final-idf-export.log`, `firmware-evidence.json` and the pinned firmware copy.

Under the standalone Dial `.pio/hardware-slice1/`:
`host-tests.log`, `final-host-tests.log`, `final-dial-build.log`,
`firmware-evidence.json` and the pinned firmware copy. That directory also retains
the earlier slice-1 COM14 recovery/serial evidence, which is not evidence for
this new integration.

Self-review checked receiver-only dispatch, reserved bytes, text bounds,
record/peer/session identity, generation-bound queues, lease expiry, legacy
fallback, separate command/metadata cursors, and control-before-metadata polling.
CodeRabbit review was explicitly requested on both draft PRs after the source
push. A completed independent review is not claimed here.

## Unclosed gates

Pair both matching builds on real hardware only after explicit authorization.
Verify a genuine local on-air FT8 decode, capture frequency and flags; record the
antenna and its band suitability. Test old/new firmware combinations, link loss,
dashboard switching and reconnection, busy decoding with audio/IQ continuity,
runtime heap/stack headroom, and physical display/encoder/touch behavior. Serial
enqueue logs are not proof of radio delivery. Neither firmware was flashed in
this continuation; COM14 still has the earlier `eb34e83` slice.

The implemented projection is latest-only. Selected history, multi-station maps,
distance/bearing and other dashboard projections remain future work. No physical
FT8 end-to-end milestone or release readiness is claimed.
