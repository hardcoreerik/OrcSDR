#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$ROOT/build/acars"
g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic \
  -I"$ROOT/apps/orcsdr-tab5/ui" \
  "$ROOT/tests/acars_core_smoke.cpp" "$ROOT/apps/orcsdr-tab5/ui/acars_core.cpp" \
  -o "$ROOT/build/acars/acars_core_smoke"
"$ROOT/build/acars/acars_core_smoke" 2>&1 | tee "$ROOT/build/acars/smoke.log"
