#!/usr/bin/env bash
# Run from any cwd; all logs are saved HERE unless a different cwd is chosen.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BASE="${TMPDIR:-/tmp}/orcsdr-dmr-device-probe"
cmake -S "$ROOT" -B "$BASE-release" -DDMR_DEVICE_PROBE_WITH_DECODER=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build "$BASE-release" --parallel
ctest --test-dir "$BASE-release" --output-on-failure --verbose > dmr_probe_tests.log 2>&1
cmake -S "$ROOT" -B "$BASE-asan" -DDMR_DEVICE_PROBE_WITH_DECODER=OFF -DDMR_DEVICE_PROBE_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build "$BASE-asan" --parallel
ctest --test-dir "$BASE-asan" --output-on-failure --verbose > dmr_probe_asan.log 2>&1
printf 'PASS: host queue tests; logs %s/dmr_probe_tests.log and %s/dmr_probe_asan.log\n' "$PWD" "$PWD"
