#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"
build_dir="$(mktemp -d)"
trap 'rm -rf "$build_dir"' EXIT

common=(-std=c++17 -Wall -Wextra -Werror -pedantic -Iapps/orcsdr-tab5/ui)
codec_sources=(tests/ft8_codec_tests.cpp apps/orcsdr-tab5/ui/ft8_codec.cpp)
ldpc_sources=(tests/ft8_ldpc_tests.cpp apps/orcsdr-tab5/ui/ft8_codec.cpp apps/orcsdr-tab5/ui/ft8_ldpc.cpp)
ldpc_decode_sources=(tests/ft8_ldpc_decode_tests.cpp apps/orcsdr-tab5/ui/ft8_codec.cpp apps/orcsdr-tab5/ui/ft8_ldpc.cpp apps/orcsdr-tab5/ui/ft8_ldpc_decode.cpp)

run_suite() {
  local name="$1"; shift
  local -a sources=("$@")
  g++ "${common[@]}" -O2 "${sources[@]}" -o "$build_dir/$name"
  "$build_dir/$name"
  g++ "${common[@]}" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    "${sources[@]}" -o "$build_dir/${name}_sanitized"
  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
    "$build_dir/${name}_sanitized"
}

run_suite ft8_codec_tests "${codec_sources[@]}"
run_suite ft8_ldpc_tests "${ldpc_sources[@]}"
run_suite ft8_ldpc_decode_tests "${ldpc_decode_sources[@]}"

echo "FT8 native codec + LDPC correctness + NMS decoder host tests: PASS"
