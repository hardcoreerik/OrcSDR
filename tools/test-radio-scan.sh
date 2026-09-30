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

cb_sources=(tests/cb_scanner_tests.cpp apps/orcsdr-tab5/ui/cb_scanner.cpp)
g++ "${common[@]}" -O2 "${cb_sources[@]}" -o "$build_dir/cb_scanner_tests"
"$build_dir/cb_scanner_tests"
g++ "${common[@]}" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  "${cb_sources[@]}" -o "$build_dir/cb_scanner_tests_sanitized"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  "$build_dir/cb_scanner_tests_sanitized"

keyboard_sources=(tests/keyboard_input_tests.cpp apps/orcsdr-tab5/ui/keyboard_input.cpp)
g++ "${common[@]}" -O2 "${keyboard_sources[@]}" -o "$build_dir/keyboard_input_tests"
"$build_dir/keyboard_input_tests"
g++ "${common[@]}" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  "${keyboard_sources[@]}" -o "$build_dir/keyboard_input_tests_sanitized"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  "$build_dir/keyboard_input_tests_sanitized"

focus_sources=(tests/focus_nav_tests.cpp apps/orcsdr-tab5/ui/focus_nav.cpp)
g++ "${common[@]}" -O2 "${focus_sources[@]}" -o "$build_dir/focus_nav_tests"
"$build_dir/focus_nav_tests"
g++ "${common[@]}" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  "${focus_sources[@]}" -o "$build_dir/focus_nav_tests_sanitized"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  "$build_dir/focus_nav_tests_sanitized"
