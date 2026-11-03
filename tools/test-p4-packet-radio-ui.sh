#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
g++ -std=c++17 -Wall -Wextra -Werror \
  -I"$root/apps/orcsdr-p4-hosyond-console/main" \
  -I"$root/apps/orcsdr-tab5/ui" -I"$root/tests/board_radio_stubs" \
  "$root/tests/p4_packet_radio_ui_test.cpp" -o "$out/test"
"$out/test"
