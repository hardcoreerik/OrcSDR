#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"
build_dir="$(mktemp -d)"
trap 'rm -rf "$build_dir"' EXIT

common=(-std=c++17 -Wall -Wextra -Werror -pedantic -Iapps/orcsdr-tab5/ui)

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

# Dashboard/model/Hunter baseline owned by the FT8 UI branch.
run_suite ft8_model_tests \
  tests/ft8_model_tests.cpp \
  apps/orcsdr-tab5/ui/ft8_model.cpp

run_suite ft8_hunter_tests \
  tests/ft8_hunter_tests.cpp \
  apps/orcsdr-tab5/ui/ft8_model.cpp \
  apps/orcsdr-tab5/ui/ft8_hunter.cpp

# Native clean-room decoder core.
run_suite ft8_mode_tests \
  tests/ft8_mode_tests.cpp \
  apps/orcsdr-tab5/ui/ft8_mode.cpp

run_suite ft8_sync_tests \
  tests/ft8_sync_tests.cpp \
  apps/orcsdr-tab5/ui/ft8_mode.cpp \
  apps/orcsdr-tab5/ui/ft8_sync.cpp

run_suite ft8_demod_tests \
  tests/ft8_demod_tests.cpp \
  apps/orcsdr-tab5/ui/ft8_mode.cpp \
  apps/orcsdr-tab5/ui/ft8_sync.cpp \
  apps/orcsdr-tab5/ui/ft8_demod.cpp

run_suite ft8_codec_tests \
  tests/ft8_codec_tests.cpp \
  apps/orcsdr-tab5/ui/ft8_codec.cpp

run_suite ft8_ldpc_tests \
  tests/ft8_ldpc_tests.cpp \
  apps/orcsdr-tab5/ui/ft8_codec.cpp \
  apps/orcsdr-tab5/ui/ft8_ldpc.cpp

run_suite ft8_ldpc_decode_tests \
  tests/ft8_ldpc_decode_tests.cpp \
  apps/orcsdr-tab5/ui/ft8_codec.cpp \
  apps/orcsdr-tab5/ui/ft8_ldpc.cpp \
  apps/orcsdr-tab5/ui/ft8_ldpc_decode.cpp

# OrcDial semantic-control regression remains independent of the Tab5 decoder.
orcdial_common=(-std=c++17 -Wall -Wextra -Werror -pedantic -Iorcdial/src)
orcdial_sources=(orcdial/tests/controller_test.cpp)
g++ "${orcdial_common[@]}" -O2 "${orcdial_sources[@]}" -o "$build_dir/orcdial_ft8_controller_tests"
"$build_dir/orcdial_ft8_controller_tests"
g++ "${orcdial_common[@]}" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  "${orcdial_sources[@]}" -o "$build_dir/orcdial_ft8_controller_tests_sanitized"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  "$build_dir/orcdial_ft8_controller_tests_sanitized"

echo "FT8 UI/model/Hunter + native ModeProfile/sync/demod/codec/LDPC/NMS + OrcDial host tests: PASS"
