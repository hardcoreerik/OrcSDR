#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"
build_dir="$(mktemp -d)"
trap 'rm -rf "$build_dir"' EXIT
common=(-std=c++17 -Wall -Wextra -Werror -pedantic -Iapps/orcsdr-tab5/ui -Itests/support/aviation)
sources=(tests/flarm_core_tests.cpp apps/orcsdr-tab5/ui/flarm_decoder_core.cpp apps/orcsdr-tab5/ui/flarm_receiver.cpp)
"${CXX:-c++}" "${common[@]}" -O2 "${sources[@]}" -o "$build_dir/flarm-tests"
"$build_dir/flarm-tests"
"${CXX:-c++}" "${common[@]}" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer "${sources[@]}" -o "$build_dir/flarm-tests-sanitized"
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$build_dir/flarm-tests-sanitized"

ui_sources=(tests/aviation_dashboard_tests.cpp apps/orcsdr-tab5/ui/adsb_dashboard.cpp apps/orcsdr-tab5/ui/dashboard_registry.cpp apps/orcsdr-tab5/ui/screen_controller.cpp apps/orcsdr-tab5/ui/radio_session.cpp)
"${CXX:-c++}" "${common[@]}" -O2 "${ui_sources[@]}" -o "$build_dir/aviation-tests"
"$build_dir/aviation-tests"
"${CXX:-c++}" "${common[@]}" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer "${ui_sources[@]}" -o "$build_dir/aviation-tests-sanitized"
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$build_dir/aviation-tests-sanitized"
