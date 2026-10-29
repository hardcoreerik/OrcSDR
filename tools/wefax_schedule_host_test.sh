#!/usr/bin/env bash
# Run from the OrcSDR checkout. Saves output beside the invocation in a log.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG="$PWD/wefax_schedule_host_test.log"
BIN="$(mktemp -d "${TMPDIR:-/tmp}/wefax-sched.XXXXXX")"
trap 'rm -rf "$BIN"' EXIT
{
  echo "WEFAX schedule/M5GFX host regression $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  g++ -std=c++17 -O2 -Wall -Wextra -Werror -I"$ROOT/apps/orcsdr-tab5/ui" \
    "$ROOT/tools/wefax_schedule_test.cpp" "$ROOT/apps/orcsdr-tab5/ui/wefax_schedule.cpp" \
    -o "$BIN/catalog"
  "$BIN/catalog"
  g++ -std=c++17 -O2 -Wall -Wextra -Werror \
    -I"$ROOT/tools/wefax_fakes" -I"$ROOT/apps/orcsdr-tab5/ui" \
    "$ROOT/tools/wefax_dashboard_host_test.cpp" \
    "$ROOT/apps/orcsdr-tab5/ui/wefax_dashboard.cpp" \
    "$ROOT/apps/orcsdr-tab5/ui/wefax_schedule.cpp" -o "$BIN/ui"
  "$BIN/ui"
} 2>&1 | tee "$LOG"
