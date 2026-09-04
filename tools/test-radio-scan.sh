#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"
build_dir="$(mktemp -d)"
trap 'rm -rf "$build_dir"' EXIT

sources=(tests/radio_scan_tests.cpp apps/orcsdr-tab5/ui/radio_session.cpp apps/orcsdr-tab5/ui/scan_engine.cpp)
common=(-std=c++17 -Wall -Wextra -Werror -pedantic -Iapps/orcsdr-tab5/ui)

g++ "${common[@]}" -O2 "${sources[@]}" -o "$build_dir/radio_scan_tests"
"$build_dir/radio_scan_tests"

g++ "${common[@]}" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  "${sources[@]}" -o "$build_dir/radio_scan_tests_sanitized"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  "$build_dir/radio_scan_tests_sanitized"
