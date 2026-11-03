#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
ui=apps/orcsdr-tab5/ui
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
flags=(-std=c++17 -O1 -g -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -fno-omit-frame-pointer -I"$ui")
g++ "${flags[@]}" tests/ft8_audio_tap_tests.cpp "$ui/ft8_audio_tap.cpp" -o "$out/audio-tap"
"$out/audio-tap"
sources=()
for name in ft8_native_backend ft8_spectral_fft ft8_mode ft8_sync ft8_demod ft8_pipeline ft8_snr ft8_message ft8_codec ft8_ldpc ft8_ldpc_decode ft8_model; do sources+=("$ui/$name.cpp"); done
g++ "${flags[@]}" tests/ft8_native_backend_tests.cpp "${sources[@]}" -o "$out/native-backend"
"$out/native-backend"
