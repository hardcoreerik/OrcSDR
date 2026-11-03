#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
g++ -std=c++17 -Wall -Wextra -Werror -pthread \
  -I"$root/tests/web_server_stubs" \
  -I"$root/apps/orcsdr-p4-hosyond-console/main" \
  "$root/tests/p4_web_server_state_test.cpp" -o "$out/test"
"$out/test"
