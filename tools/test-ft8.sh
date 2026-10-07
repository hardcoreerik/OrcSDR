#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"
build_dir="$(mktemp -d)"
trap 'rm -rf "$build_dir"' EXIT

common=(-std=c++17 -Wall -Wextra -Werror -pedantic -Iapps/orcsdr-tab5/ui)
sources=(tests/ft8_codec_tests.cpp apps/orcsdr-tab5/ui/ft8_codec.cpp)

g++ "${common[@]}" -O2 "${sources[@]}" -o "$build_dir/ft8_codec_tests"
"$build_dir/ft8_codec_tests"

g++ "${common[@]}" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  "${sources[@]}" -o "$build_dir/ft8_codec_tests_sanitized"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  "$build_dir/ft8_codec_tests_sanitized"

echo "FT8 native codec host tests: PASS"
