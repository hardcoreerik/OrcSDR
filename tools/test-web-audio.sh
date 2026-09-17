#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"
build_dir="$(mktemp -d)"
trap 'rm -rf "$build_dir"' EXIT
common=(-std=c++17 -Wall -Wextra -Werror -pedantic -Iapps/orcsdr-tab5/ui)
g++ "${common[@]}" -O2 tests/web_command_tests.cpp -o "$build_dir/web_command_tests"
"$build_dir/web_command_tests"
g++ "${common[@]}" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  tests/web_command_tests.cpp -o "$build_dir/web_command_tests_sanitized"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  "$build_dir/web_command_tests_sanitized"
g++ "${common[@]}" -O2 tests/web_audio_tests.cpp -o "$build_dir/web_audio_tests"
"$build_dir/web_audio_tests"
g++ "${common[@]}" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  tests/web_audio_tests.cpp -o "$build_dir/web_audio_tests_sanitized"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  "$build_dir/web_audio_tests_sanitized"
