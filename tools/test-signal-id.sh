#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
log="$PWD/signal-id-host-test.log"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# Keep the full compiler and execution transcript beside the invocation.
{
  printf 'Signal ID host test (%s)\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  cxx="${CXX:-g++}"
  "${cxx}" -std=c++17 -O2 -Wall -Wextra -Werror \
    "$root/tests/signal_id_tests.cpp" \
    "$root/apps/orcsdr-tab5/ui/signal_id.cpp" \
    -o "$tmp/test"
  "$tmp/test"
  echo 'optimized: PASS'
  "${cxx}" -std=c++17 -O1 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$root/tests/signal_id_tests.cpp" \
    "$root/apps/orcsdr-tab5/ui/signal_id.cpp" \
    -o "$tmp/test-sanitized"
  ASAN_OPTIONS=detect_leaks=1 "$tmp/test-sanitized"
  echo 'ASan/UBSan: PASS'
} 2>&1 | tee "$log"
