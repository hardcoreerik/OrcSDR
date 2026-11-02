#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/build/hfdl-dashboard"
mkdir -p "$OUT"
LOG="$OUT/test.log"
{
  echo "HFDL test start: $(date -u +%FT%TZ)"
  echo "Compiler: ${CXX:-c++}"
  "${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror -pthread \
    "$ROOT/apps/orcsdr-tab5/tests/hfdl_dashboard_test.cpp" \
    "$ROOT/apps/orcsdr-tab5/ui/hfdl_dashboard_model.cpp" \
    "$ROOT/apps/orcsdr-tab5/ui/hfdl_dashboard_ui.cpp" \
    "$ROOT/apps/orcsdr-tab5/hfdl/hfdl_decoder.cpp" \
    -o "$OUT/hfdl-dashboard-tests"
  "$OUT/hfdl-dashboard-tests"
  "${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror \
    -DORCSDR_HFDL_HOST -I"$ROOT/tests/hfdl_ui_stubs" \
    "$ROOT/tests/hfdl_dashboard_render_tests.cpp" \
    "$ROOT/apps/orcsdr-tab5/ui/hfdl_dashboard.cpp" \
    "$ROOT/apps/orcsdr-tab5/ui/hfdl_dashboard_model.cpp" \
    "$ROOT/apps/orcsdr-tab5/ui/hfdl_dashboard_ui.cpp" \
    -o "$OUT/hfdl-render-tests"
  "$OUT/hfdl-render-tests"
  echo "HFDL test end: $(date -u +%FT%TZ)"
} 2>&1 | tee "$LOG"
