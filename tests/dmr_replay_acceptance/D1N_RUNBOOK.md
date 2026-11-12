# Phase D1-N — pinned noise/AM and four-fixture native IQ gate

**Scope:** This work owns only `tests/dmr_replay_acceptance/**`. It builds no replacement DMR receiver and does not modify firmware, native D0/D1, frontend, P25, M5GFX, common CI, or private research data. All binary IQ and runtime logs stay **outside** this Git checkout.

## Acceptance state and evidence policy

The two 48 kS/s real-air direct-mode captures were previously decoded by native OrcSDR in a **separate** public real-IQ CI run; see `docs/dmr/REAL_IQ_PUBLIC_CAPTURE_ACCEPTANCE_2026-11-11.md`. That report documented source ID `3024109`, talkgroup `99`, color code `1`, and equal event fingerprints at 257 and 4096 input samples/chunk. Those are **previous OrcSDR observations**, not fresh D1-N measurements or arbitrary upstream assertions.

The **four-fixture gate is not passed** until the *current* native `dmr_d1_replay_bridge` executes both direct-mode captures **and** both exact negative controls in Release **and** ASan/UBSan, with five input chunk sizes and equal canonical ordered-event SHA-256 digests. The provenance pins in the immutable source manifest must pass before running any decoder. No generated/mock bridge event may substitute for native output.

The pinned controls are reproducible using the read-only private REA recipe:

- `hardcoreerik/REA-Results`, branch `chatgpt/digital-voice-rsa-rea-2026-11-01`, `inbox/fixtures/digital-voice/AUTO/generate_controls.py`, Git blob `ae300001ca98e0e8174c88d9a496025e6cb5ad3a`.
- `auto-negative-noise`: 384,000 CS16-LE bytes, 96,000 complex samples; Git blob SHA-1 `3d872e2afb5aa8759520657c2c1c25fbd03acab7`, SHA-256 `ae4f5f6d0ab0f3b8aec9a94cfed8dbadbbc13f94db8758fc2c5bd03b1bdcef71`.
- `auto-negative-am`: 384,000 CS16-LE bytes, 96,000 complex samples; Git blob SHA-1 `f8873579fe47229866fb2b1576726a22bfc4aa5d`, SHA-256 `32d7ea0c83f7ca4168337c90c06e491f1a259dce8241fd8d4b1c5839986aa87e`.

`negative_control_repro.py` independently implements this exact algorithm using only the Python standard library, reads the **existing** `tests/digital_voice_replay/manifest.json` as authoritative, and fails if either result differs. It never updates expected pins. By default it performs the byte generation **in memory** and writes **no IQ**. Optional `--write-external-root` creates the two generated captures exclusively outside OrcSDR, refusing overwrites, if the original REA checkout is unavailable; the generated exact-match controls are permitted as sources only when **both manifest hashes match**. Keep both direct-mode real recordings separately pinned as well.

## Run from the OrcSDR repository root

Use a checked-out `feature/dmr-digital-voice-foundation` branch. Pull/rebase carefully when no other agents own overlapping paths:

```bash
# Git repository must have origin set to hardcoreerik/OrcSDR.
git fetch origin feature/dmr-digital-voice-foundation
git pull --rebase origin feature/dmr-digital-voice-foundation
```

### 1. Prepare private data and external evidence folder

```bash
# This path is a separate REA-Results checkout, never inside OrcSDR.
ROOT=/absolute/path/REA-Results/inbox/fixtures/digital-voice
# Must contain DMR/ and AUTO/ with the four manifest-named files.
EVIDENCE_DIR=$(mktemp -d /tmp/orcsdr-d1n.XXXXXXXX)
printf 'Runtime logs and JSON: %s\n' "$EVIDENCE_DIR"
```

Alternatively, recreate *only* the exact pinned synthetic controls in an external fixture root. An existing file will **not** be overwritten:

```bash
python3 tests/dmr_replay_acceptance/negative_control_repro.py \
  --write-external-root "$ROOT" \
  --report "$EVIDENCE_DIR/negative-reproduction.json" \
  > "$EVIDENCE_DIR/negative-reproduction.log" 2>&1
```

If the two AUTO files already exist, verify them without writing:

```bash
python3 tests/dmr_replay_acceptance/negative_control_repro.py \
  --external-root "$ROOT" \
  --report "$EVIDENCE_DIR/negative-source-comparison.json" \
  > "$EVIDENCE_DIR/negative-source-comparison.log" 2>&1
```

Or regenerate into memory and compare the pins without accessing private files:

```bash
python3 tests/dmr_replay_acceptance/negative_control_repro.py \
  --report "$EVIDENCE_DIR/negative-regenerated-hashes.json" \
  > "$EVIDENCE_DIR/negative-regenerated-hashes.log" 2>&1
```

### 2. Build the **existing native decoder**, separately in Release and sanitizers

```bash
cmake -S components/orcsdr_dmr/d1_integration \
  -B build/dmr-d1n-release -DCMAKE_BUILD_TYPE=Release \
  > "$EVIDENCE_DIR/cmake-release.log" 2>&1
cmake --build build/dmr-d1n-release --parallel \
  > "$EVIDENCE_DIR/build-release.log" 2>&1
ctest --test-dir build/dmr-d1n-release --output-on-failure \
  > "$EVIDENCE_DIR/ctest-release.log" 2>&1

cmake -S components/orcsdr_dmr/d1_integration \
  -B build/dmr-d1n-asan -DCMAKE_BUILD_TYPE=Debug \
  -DDMR_D1_INTEGRATION_SANITIZE=ON \
  > "$EVIDENCE_DIR/cmake-asan.log" 2>&1
cmake --build build/dmr-d1n-asan --parallel \
  > "$EVIDENCE_DIR/build-asan.log" 2>&1
ctest --test-dir build/dmr-d1n-asan --output-on-failure \
  > "$EVIDENCE_DIR/ctest-asan.log" 2>&1
```

Check **both** CTest exit statuses before proceeding. The CMake sanitizer option is defined in the existing D1 integration component; the Python test suite does not instrument C++.

### 3. Test D1-N harness and execute all 40 native replays

```bash
python3 -m unittest discover -s tests/dmr_replay_acceptance \
  -p 'test_*.py' -v \
  > "$EVIDENCE_DIR/python-host-tests.log" 2>&1

python3 tests/dmr_replay_acceptance/d1n_matrix.py \
  --fixtures-root "$ROOT" \
  --release-bridge "$(pwd)/build/dmr-d1n-release/dmr_d1_replay_bridge" \
  --sanitizer-bridge "$(pwd)/build/dmr-d1n-asan/dmr_d1_replay_bridge" \
  --chunk-sizes 1 7 257 1024 4096 \
  --timeout 240 \
  --report "$EVIDENCE_DIR/d1n-native-matrix.json" \
  --log "$EVIDENCE_DIR/d1n-native-matrix.log"
```

**Exactly 40 combinations** = 4 fixture IDs × 5 chunk sizes × 2 build modes. Each source file must pass original byte length / Git blob SHA-1 / any pinned SHA-256 before native replay. The native executable consumes input via CF32-LE stdin converted from CS16-LE **without resampling**; replay verifies input bytes again during streaming, and its terminal JSONL summary must acknowledge the exact sample count. D1-N uses the existing `acceptance.run_bridge()` and `real_iq_gate` evidence semantics. A test-only fake bridge is used **solely** for Python unit tests and cannot satisfy native acceptance.

Exit codes: `0` means the defined full native checks pass; `2` means FAIL or REVIEW; `3` means BLOCKED (missing private data, native binaries or existing harness dependency). Missing results are `null`/`BLOCKED`, never zero. The report and log are always outside both the OrcSDR checkout and the private fixture directory; output files are created exclusively, so choose fresh paths for subsequent runs.

### 4. Interpret the matrix

- For **both** real-air direct-mode captures, require independent actual native output containing **source ID 3024109, group/talkgroup 99, color code 1**, sync and FEC-plus-integrity evidence. These checks are evaluated *after* decode, never supplied to the receiver.
- For **both exact controls**, `false_identity_detections == 0`, `unverified_identity_claims == 0`, and both source and talkgroup lists must be empty. `false_sync_events_on_non_dmr_control` counts decoder-emitted sync indications on known synthetic non-DMR IQ; a nonzero count is measurable but not by itself a validated identity. Do not treat missing metrics as zero.
- `emitted_bursts_with_failed_fec_or_check` counts **only those failed bursts for which the native JSONL bridge emitted a burst event**, when the updated parser is present. `total_decoder_rejected_candidates` remains `null` because JSONL v1 omits candidates discarded without an event. Neither value should be invented.
- The ordered event trace fingerprint includes all emitted event records and sample offsets; every capture must have **identical** fingerprint and comparable metric counts at chunk sizes 1, 7, 257, 1024, 4096, **within each build mode**. Release and sanitizer must each pass. Comparisons between two separately compiled binaries should be reported rather than assumed identical.
- `upstream_expected` values in the source manifest are third-party assertions; `expected_recovered_identity_from_prior_public_real_iq_ci` in the report is separately sourced earlier **OrcSDR** evidence. Only per-run `native_replay_matrix[*].metrics` are measurements from *this* execution.

## Troubleshooting / known gaps

If either positive recording is missing, an exact negative control is missing/mismatched, or either native executable is absent, the matrix remains **BLOCKED** and no native decoder is invoked. An existing externally archived two-positive public CI success is not sufficient to declare a new four-fixture native run passed. If `--chunk-sizes 1` is slow under sanitizers, increase the **timeout**; do not skip that required input chunk size. The source harness still rehashes every input and checks all ordered events. This phase does not benchmark ESP32-P4 USB, SDR hardware, real RF sensitivity or full DMR calls.
