#!/usr/bin/env bash
# Run from the OrcSDR repo root. Writes test log next to this script.
set -euo pipefail
cd "$(dirname "$0")/.."
log="tools/wefax_host_test.log"
exe="$(mktemp "${TMPDIR:-/tmp}/orcsdr-wefax-test.XXXXXX")"
trap 'rm -f "$exe"' EXIT
{
  echo "WEFAX host test starting $(date -u +%FT%TZ)"
  g++ -std=c++17 -O2 -Wall -Wextra -Werror -Iapps/orcsdr-tab5/ui \
    tools/wefax_core_test.cpp apps/orcsdr-tab5/ui/wefax_core.cpp -o "$exe"
  WEFAX_TEST_PGM="${TMPDIR:-/tmp}/orcsdr_wefax_reference.pgm" "$exe"
} 2>&1 | tee "$log"
