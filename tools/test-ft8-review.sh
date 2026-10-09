#!/usr/bin/env bash
# Focused regressions for the receive, persistence and control review fixes. No downloads or hardware access.
set -euo pipefail
cd "$(dirname "$0")/.."
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT
ui=apps/orcsdr-tab5/ui
run() {
  local name=$1; shift
  for variant in optimized sanitized; do
    local flags=(-O2)
    if [[ $variant == sanitized ]]; then flags=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer); fi
    g++ -std=c++17 -Wall -Wextra -Werror -pedantic -I"$ui" "${flags[@]}" "$@" -o "$build/$name-$variant"
    "$build/$name-$variant"
  done
}
run js8_native_backend tests/js8_native_backend_tests.cpp \
  "$ui"/{js8_native_backend,js8_frontend,js8_mode,js8_frame,js8_sync,js8_spectral,js8_demod,js8_decoder,js8_codec,js8_message,js8_osd,js8_fec,ft8_spectral_fft}.cpp
run js8_decoder tests/js8_decoder_tests.cpp "$ui"/{js8_decoder,js8_codec,js8_message,js8_osd,js8_fec}.cpp
run heard_db tests/ft8_heard_db_tests.cpp "$ui/ft8_heard_db.cpp"
run dashboard -Itests/ft8_ui_stubs tests/ft8_dashboard_tests.cpp \
  "$ui"/{ft8_dashboard,ft8_model,ft8_conditions,ft8_hunter,ft8_tuning,ft8_info_text,ft8_world_data,focus_nav}.cpp
run info tests/ft8_info_text_tests.cpp "$ui/ft8_info_text.cpp"
run controller orcdial/tests/controller_test.cpp
run model tests/ft8_model_tests.cpp "$ui/ft8_model.cpp"
run tuning tests/ft8_tuning_tests.cpp "$ui/ft8_tuning.cpp"
echo 'Focused FT8/FT4/JS8 review regressions: PASS (optimized + ASan/UBSan)'
