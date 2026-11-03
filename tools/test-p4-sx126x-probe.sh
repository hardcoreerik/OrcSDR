#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
component="$root/ports/p4/components/orcsdr_sx126x"
gcc -std=c11 -Wall -Wextra -Werror \
  -I"$root/tests/board_radio_stubs" -I"$root/ports/p4/components/orcsdr_board_hosyond/include" \
  -I"$component/include" -I"$component/vendor" \
  "$root/tests/p4_sx126x_probe_test.c" "$component/orcsdr_sx126x.c" "$component/vendor/sx126x.c" -o "$out/test"
"$out/test"
