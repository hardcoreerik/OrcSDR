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

backend_sources=(tests/ft8_backend_tests.cpp apps/orcsdr-tab5/ui/ft8_model.cpp)
g++ "${common[@]}" -O2 "${backend_sources[@]}" -o "$build_dir/ft8_backend_tests"
"$build_dir/ft8_backend_tests"
g++ "${common[@]}" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  "${backend_sources[@]}" -o "$build_dir/ft8_backend_tests_sanitized"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  "$build_dir/ft8_backend_tests_sanitized"

conditions_sources=(tests/ft8_conditions_tests.cpp apps/orcsdr-tab5/ui/ft8_model.cpp apps/orcsdr-tab5/ui/ft8_conditions.cpp apps/orcsdr-tab5/ui/ft8_adif.cpp)
g++ "${common[@]}" -O2 "${conditions_sources[@]}" -o "$build_dir/ft8_conditions_tests"
"$build_dir/ft8_conditions_tests"
g++ "${common[@]}" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer   "${conditions_sources[@]}" -o "$build_dir/ft8_conditions_tests_sanitized"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1   "$build_dir/ft8_conditions_tests_sanitized"

hunter_sources=(tests/ft8_hunter_tests.cpp apps/orcsdr-tab5/ui/ft8_model.cpp apps/orcsdr-tab5/ui/ft8_hunter.cpp)
g++ "${common[@]}" -O2 "${hunter_sources[@]}" -o "$build_dir/ft8_hunter_tests"
"$build_dir/ft8_hunter_tests"
g++ "${common[@]}" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  "${hunter_sources[@]}" -o "$build_dir/ft8_hunter_tests_sanitized"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  "$build_dir/ft8_hunter_tests_sanitized"

# Native clean-room decoder core, spectral FFT, audio tap and the bound backend.
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

run_suite ft8_mode_tests \
  tests/ft8_mode_tests.cpp \
  apps/orcsdr-tab5/ui/ft8_mode.cpp

run_suite js8_mode_tests \
  tests/js8_mode_tests.cpp \
  apps/orcsdr-tab5/ui/js8_mode.cpp

run_suite js8_frame_tests \
  tests/js8_frame_tests.cpp \
  apps/orcsdr-tab5/ui/js8_mode.cpp \
  apps/orcsdr-tab5/ui/js8_frame.cpp

run_suite js8_demod_tests \
  tests/js8_demod_tests.cpp \
  apps/orcsdr-tab5/ui/js8_mode.cpp \
  apps/orcsdr-tab5/ui/js8_frame.cpp \
  apps/orcsdr-tab5/ui/js8_demod.cpp

run_suite js8_fec_reconstruct_tests \
  tests/js8_fec_reconstruct_tests.cpp

run_suite js8_tone_map_tests \
  tests/js8_tone_map_tests.cpp

g++ "${common[@]}" -O2 tools/js8-tone-map.cpp \
  apps/orcsdr-tab5/ui/js8_mode.cpp \
  apps/orcsdr-tab5/ui/js8_frame.cpp \
  -o "$build_dir/js8-tone-map"

g++ "${common[@]}" -O2 tools/js8-fec-reconstruct.cpp -o "$build_dir/js8-fec-reconstruct"

run_suite ft8_sync_tests \
  tests/ft8_sync_tests.cpp \
  apps/orcsdr-tab5/ui/ft8_mode.cpp \
  apps/orcsdr-tab5/ui/ft8_sync.cpp

run_suite ft8_demod_tests \
  tests/ft8_demod_tests.cpp \
  apps/orcsdr-tab5/ui/ft8_mode.cpp \
  apps/orcsdr-tab5/ui/ft8_sync.cpp \
  apps/orcsdr-tab5/ui/ft8_demod.cpp

run_suite ft8_spectral_tests \
  tests/ft8_spectral_tests.cpp \
  apps/orcsdr-tab5/ui/ft8_mode.cpp \
  apps/orcsdr-tab5/ui/ft8_sync.cpp \
  apps/orcsdr-tab5/ui/ft8_demod.cpp \
  apps/orcsdr-tab5/ui/ft8_spectral.cpp \
  apps/orcsdr-tab5/ui/ft8_pipeline.cpp \
  apps/orcsdr-tab5/ui/ft8_snr.cpp \
  apps/orcsdr-tab5/ui/ft8_message.cpp \
  apps/orcsdr-tab5/ui/ft8_codec.cpp \
  apps/orcsdr-tab5/ui/ft8_ldpc.cpp \
  apps/orcsdr-tab5/ui/ft8_ldpc_decode.cpp

run_suite ft8_pipeline_tests \
  tests/ft8_pipeline_tests.cpp \
  apps/orcsdr-tab5/ui/ft8_mode.cpp \
  apps/orcsdr-tab5/ui/ft8_sync.cpp \
  apps/orcsdr-tab5/ui/ft8_demod.cpp \
  apps/orcsdr-tab5/ui/ft8_pipeline.cpp \
  apps/orcsdr-tab5/ui/ft8_snr.cpp \
  apps/orcsdr-tab5/ui/ft8_message.cpp \
  apps/orcsdr-tab5/ui/ft8_codec.cpp \
  apps/orcsdr-tab5/ui/ft8_ldpc.cpp \
  apps/orcsdr-tab5/ui/ft8_ldpc_decode.cpp

run_suite ft8_message_tests \
  tests/ft8_message_tests.cpp \
  apps/orcsdr-tab5/ui/ft8_codec.cpp \
  apps/orcsdr-tab5/ui/ft8_message.cpp

run_suite ft8_codec_tests \
  tests/ft8_codec_tests.cpp \
  apps/orcsdr-tab5/ui/ft8_codec.cpp

run_suite ft8_ldpc_tests \
  tests/ft8_ldpc_tests.cpp \
  apps/orcsdr-tab5/ui/ft8_ldpc.cpp \
  apps/orcsdr-tab5/ui/ft8_codec.cpp

run_suite ft8_ldpc_decode_tests \
  tests/ft8_ldpc_decode_tests.cpp \
  apps/orcsdr-tab5/ui/ft8_ldpc.cpp \
  apps/orcsdr-tab5/ui/ft8_codec.cpp \
  apps/orcsdr-tab5/ui/ft8_ldpc_decode.cpp

run_suite ft8_audio_tap_tests \
  tests/ft8_audio_tap_tests.cpp \
  apps/orcsdr-tab5/ui/ft8_audio_tap.cpp

run_suite ft8_spectral_fft_tests \
  tests/ft8_spectral_fft_tests.cpp \
  apps/orcsdr-tab5/ui/ft8_spectral_fft.cpp \
  apps/orcsdr-tab5/ui/ft8_spectral.cpp \
  apps/orcsdr-tab5/ui/ft8_mode.cpp

run_suite ft8_native_backend_tests \
  tests/ft8_native_backend_tests.cpp \
  apps/orcsdr-tab5/ui/ft8_native_backend.cpp \
  apps/orcsdr-tab5/ui/ft8_spectral_fft.cpp \
  apps/orcsdr-tab5/ui/ft8_mode.cpp \
  apps/orcsdr-tab5/ui/ft8_sync.cpp \
  apps/orcsdr-tab5/ui/ft8_demod.cpp \
  apps/orcsdr-tab5/ui/ft8_pipeline.cpp \
  apps/orcsdr-tab5/ui/ft8_snr.cpp \
  apps/orcsdr-tab5/ui/ft8_message.cpp \
  apps/orcsdr-tab5/ui/ft8_codec.cpp \
  apps/orcsdr-tab5/ui/ft8_ldpc.cpp \
  apps/orcsdr-tab5/ui/ft8_ldpc_decode.cpp \
  apps/orcsdr-tab5/ui/ft8_model.cpp

# SNR estimator against synthetic FT8 signals of known strength (fails if the estimate drifts more than 1.5 dB)
g++ "${common[@]}" -O2 tools/ft8-snr-sweep.cpp   apps/orcsdr-tab5/ui/ft8_native_backend.cpp apps/orcsdr-tab5/ui/ft8_spectral_fft.cpp apps/orcsdr-tab5/ui/ft8_mode.cpp   apps/orcsdr-tab5/ui/ft8_sync.cpp apps/orcsdr-tab5/ui/ft8_demod.cpp apps/orcsdr-tab5/ui/ft8_pipeline.cpp apps/orcsdr-tab5/ui/ft8_snr.cpp   apps/orcsdr-tab5/ui/ft8_message.cpp apps/orcsdr-tab5/ui/ft8_codec.cpp apps/orcsdr-tab5/ui/ft8_ldpc.cpp apps/orcsdr-tab5/ui/ft8_ldpc_decode.cpp   apps/orcsdr-tab5/ui/ft8_model.cpp -o "$build_dir/ft8_snr_sweep"
"$build_dir/ft8_snr_sweep" --check

orcdial_common=(-std=c++17 -Wall -Wextra -Werror -pedantic -Iorcdial/src)
orcdial_sources=(orcdial/tests/controller_test.cpp)
g++ "${orcdial_common[@]}" -O2 "${orcdial_sources[@]}" -o "$build_dir/orcdial_ft8_controller_tests"
"$build_dir/orcdial_ft8_controller_tests"
g++ "${orcdial_common[@]}" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  "${orcdial_sources[@]}" -o "$build_dir/orcdial_ft8_controller_tests_sanitized"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  "$build_dir/orcdial_ft8_controller_tests_sanitized"

echo "FT8 model, decoder-backend seam, Hunter, and OrcDial semantic controller host tests: PASS"
