# DMR real-air IQ verification and replay matrix

**Scope:** independent public-capture acquisition and recorded-evidence orchestration. No new SDR demodulator, decoder, or substitute harness. All committed files live in `tests/dmr_real_iq_validation/**` on `feature/dmr-digital-voice-foundation`.

The four real-air channelized recordings originate in [GopherTrunk](https://github.com/MattCheramie/GopherTrunk), source commit `f06bfff493f47fdeb69807c8b08b2b32a253839f`, directory `cmd/gophertrunk/testdata/`. Their values are matched against OrcSDR's **existing** `tests/digital_voice_replay/manifest.json`. The public-source binaries were independently retrieved via a connected GitHub source and checked byte-by-byte against their expected length, Git blob SHA-1 and newly calculated SHA-256; see `REAL_IQ_RESULTS.md`. **The native D1 decoder did not run against these recordings in this execution environment.** Public download provenance and decoding are distinct gates.

## Pipeline and trust boundary

`48 kS/s CS16 LE` or `48 kS/s CF32 LE` -> existing `acceptance._stream_verified()` format normalization -> **unmodified** `dmr_d1_replay_bridge` -> production D1 4FSK front end -> hard dibits -> protocol frame/FEC/RS/CRC -> strict JSONL -> existing `acceptance.aggregate_events()` -> this script's chunk-comparison matrix.

Direct-mode source radio ID 3024109, talkgroup 99 and color code 1 are **UPSTREAM_EXPECTED**, not measurements by OrcSDR. Valid identity requires existing `identity_valid` AND FEC AND integrity check. Time-slot labels must respect CACH confidence. Tier III remains exploratory: do not equate a Tier II decoder's limited events with complete Tier III signaling. These recordings are **channelized real RF**, not untouched RTL-SDR full-rate captures.

The `replay_matrix.py` source pin is fixed; it downloads only the four allowlisted DMR paths from the pinned public commit, and refuses generated data under the repository root. Each download is streamed into an external temporary `.part` file, then atomically moved only after the size/blob pin validates. All failures are preserved as error evidence, not hidden as success. The script imports the existing acceptance harness to reverify inputs and execute native IQ replay. It never produces replacement IQ or binary artifacts inside OrcSDR.

## Reproduction: Linux

Run from the **OrcSDR repository root** on Linux with Python 3.10+, Git, CMake, GCC or Clang. Internet connectivity is needed only for public IQ acquisition, not the unit or decoder-host tests.

```bash
# Ensure this branch is the target; do not merge main here.
git fetch origin
git switch feature/dmr-digital-voice-foundation
git pull --ff-only origin feature/dmr-digital-voice-foundation

# Build the real D0/D1 native bridge in Release and establish baseline.
cmake -S components/orcsdr_dmr/d1_integration -B build/dmr-d1 -DCMAKE_BUILD_TYPE=Release
cmake --build build/dmr-d1 --parallel
ctest --test-dir build/dmr-d1 --output-on-failure

# Independent production-linked ETSI tests.
cmake -S tests/dmr_protocol_conformance -B build/dmr-conformance -DCMAKE_BUILD_TYPE=Release
cmake --build build/dmr-conformance --parallel
ctest --test-dir build/dmr-conformance --output-on-failure

# Source acquisition: public, pinned, outside the repository.
python3 tests/dmr_real_iq_validation/replay_matrix.py fetch \
  --fixtures-root /tmp/orcsdr-dmr-real-iq \
  --results-root /tmp/orcsdr-dmr-real-iq-results

# Native decoder replay: 4 files x 5 chunk sizes = 20 exact-binary runs.
python3 tests/dmr_real_iq_validation/replay_matrix.py run \
  --fixtures-root /tmp/orcsdr-dmr-real-iq \
  --results-root /tmp/orcsdr-dmr-real-iq-results \
  --bridge "$(pwd)/build/dmr-d1/dmr_d1_replay_bridge" \
  --chunk-sizes 1 7 257 1024 4096

# Orchestrator tests do not require RF or network.
python3 -m unittest discover -s tests/dmr_real_iq_validation/tests -p 'test_*.py' -v
```

Run the sanitizer baseline independently (host-only, not a calibrated receiver benchmark):

```bash
cmake -S components/orcsdr_dmr/d1_integration -B build/dmr-d1-asan \
  -DCMAKE_BUILD_TYPE=Debug -DDMR_D1_INTEGRATION_SANITIZE=ON
cmake --build build/dmr-d1-asan --parallel
ctest --test-dir build/dmr-d1-asan --output-on-failure
```

**Existing harness direct reproduction:**

```bash
python3 tests/dmr_replay_acceptance/acceptance.py verify --fixtures-root /tmp/orcsdr-dmr-real-iq --id dmr-directmode-keyup
python3 tests/dmr_replay_acceptance/acceptance.py run --fixtures-root /tmp/orcsdr-dmr-real-iq \
  --id dmr-directmode-keyup --bridge-command "$(pwd)/build/dmr-d1/dmr_d1_replay_bridge" \
  --chunk-samples 257 --report /tmp/orcsdr-dmr-real-iq-results/direct-keyup-existing-harness.json
```

## Windows / PowerShell

Python 3.10+, CMake and a configured C++ compiler are required. From the repository root:

```powershell
cmake -S components/orcsdr_dmr/d1_integration -B build/dmr-d1 -DCMAKE_BUILD_TYPE=Release
cmake --build build/dmr-d1 --config Release --parallel
ctest --test-dir build/dmr-d1 -C Release --output-on-failure

$root = Join-Path $env:TEMP 'orcsdr-dmr-real-iq'
$out = Join-Path $env:TEMP 'orcsdr-dmr-real-iq-results'
python tests/dmr_real_iq_validation/replay_matrix.py fetch --fixtures-root "$root" --results-root "$out"
# For Visual Studio generators the .exe normally lives in the Release subfolder:
python tests/dmr_real_iq_validation/replay_matrix.py run --fixtures-root "$root" --results-root "$out" --bridge "$(Get-Location)\build\dmr-d1\Release\dmr_d1_replay_bridge.exe"
python -m unittest discover -s tests/dmr_real_iq_validation/tests -p 'test_*.py' -v
```

With Ninja or other single-configuration generators, change `--bridge` to `build\dmr-d1\dmr_d1_replay_bridge.exe`. Windows sanitizers are compiler dependent; use WSL for the documented GCC/Clang sanitizer gate.

## WSL2

Run the **Linux** instructions inside your WSL distribution and use `/tmp` as shown. Prefer native Linux filesystem paths for building and IQ data, rather than `/mnt/c`, to avoid file performance artifacts. WSL success is host replay evidence, **not** hardware behavior on the ESP32-P4.

## Optional private synthetic controls

The pinned noise and AM controls in `tests/digital_voice_replay/manifest.json` are **not** part of the public GopherTrunk download set. If an authorized private `REA-Results` worktree supplies the exact `AUTO/` files, add `--include-controls` to `verify` or `run`. Missing controls produce BLOCKED, never synthetic substitutions.

## Reports and failures

Generated outputs are written **only** to the selected external `--results-root`:

- `public_download_verification.json`: source integrity and SHA-256 results on the **running machine**.
- `local_byte_verification.json`: exact size/blob verification of local files.
- `<fixture>-chunk-<samples>.json` and matching `.log`: native decoder metrics, wall time, chunk size and errors, or explicit NOT MEASURED.
- `real_iq_matrix_summary.json`: cross-chunk event SHA-256 consistency and conservative PASS/REVIEW/FAILED/BLOCKED summary.

A `PASS` means only that the current stated gates passed. It is not calibrated sensitivity, full Tier III support, hardware RAM usage, an audio decode, or independent proof of RF identities in an untrusted fixture. Use CTest, the individual JSON results and `DMR_DEFECTS.md` to investigate errors without editing production decoder modules from this write scope. Exit codes: `0` PASS/source verified, `2` review/failure, `3` blocked/unavailable.
