#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
 -I"$root/tests/lora_tx_ui_stubs" -I"$root/tests/ft8_ui_stubs" -I"$root/apps/orcsdr-tab5/ui" \
 "$root/tests/lora_rx_data_views_test.cpp" -o "$out/test"
"$out/test"
