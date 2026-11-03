#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
gcc -std=c11 -Wall -Wextra -Werror \
  -I"$root/tests/board_radio_stubs" -I"$root/ports/p4/components/orcsdr_board_hosyond/include" \
  "$root/tests/p4_board_radio_control_test.c" \
  "$root/ports/p4/components/orcsdr_board_hosyond/src/orcsdr_board_control.c" -o "$out/test"
"$out/test"
