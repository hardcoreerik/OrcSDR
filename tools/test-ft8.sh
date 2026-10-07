#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"
build_dir="$(mktemp -d)"
trap 'rm -rf "$build_dir"' EXIT

common=(-std=c++17 -Wall -Wextra -Werror -pedantic -Iapps/orcsdr-tab5/ui)

model_sources=(tests/ft8_model_tests.cpp apps/orcsdr-tab5/ui/ft8_model.cpp)
g++ "${common[@]}" -O2 "${model_sources[@]}" -o "$build_dir/ft8_model_tests"
"$build_dir/ft8_model_tests"
g++ "${common[@]}" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  "${model_sources[@]}" -o "$build_dir/ft8_model_tests_sanitized"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  "$build_dir/ft8_model_tests_sanitized"

hunter_sources=(tests/ft8_hunter_tests.cpp apps/orcsdr-tab5/ui/ft8_model.cpp apps/orcsdr-tab5/ui/ft8_hunter.cpp)
g++ "${common[@]}" -O2 "${hunter_sources[@]}" -o "$build_dir/ft8_hunter_tests"
"$build_dir/ft8_hunter_tests"
g++ "${common[@]}" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  "${hunter_sources[@]}" -o "$build_dir/ft8_hunter_tests_sanitized"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  "$build_dir/ft8_hunter_tests_sanitized"

orcdial_common=(-std=c++17 -Wall -Wextra -Werror -pedantic -Iorcdial/src)
orcdial_sources=(orcdial/tests/controller_test.cpp)
g++ "${orcdial_common[@]}" -O2 "${orcdial_sources[@]}" -o "$build_dir/orcdial_ft8_controller_tests"
"$build_dir/orcdial_ft8_controller_tests"
g++ "${orcdial_common[@]}" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  "${orcdial_sources[@]}" -o "$build_dir/orcdial_ft8_controller_tests_sanitized"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  "$build_dir/orcdial_ft8_controller_tests_sanitized"

echo "FT8 model, Hunter, and OrcDial semantic controller host tests: PASS"
