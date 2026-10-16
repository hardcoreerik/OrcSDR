#!/usr/bin/env bash
# Host tests for the ACARS dashboard: model, demo data, touch controller, layout, and the real renderer
# driven through a recording M5GFX stand-in. No ESP-IDF, display or hardware needed.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UI="$ROOT/apps/orcsdr-tab5/ui"
OUT="$ROOT/build/acars"
mkdir -p "$OUT"
FLAGS=(-std=c++17 -O1 -g -Wall -Wextra -Werror -pedantic -pthread)
MODEL=("$UI/acars_dashboard_model.cpp" "$UI/acars_demo_data.cpp" "$UI/acars_dashboard_layout.cpp" "$UI/acars_dashboard_ui.cpp" "$UI/acars_dirty_rects.cpp")

run() {
  local name="$1"; shift
  echo "== $name"
  g++ "${FLAGS[@]}" "$@" -o "$OUT/$name"
  "$OUT/$name" 2>&1 | tee -a "$OUT/ui-tests.log"
}

: > "$OUT/ui-tests.log"
run acars_dashboard_model_tests -I"$UI" "$ROOT/tests/acars_dashboard_model_tests.cpp" "${MODEL[@]}"
if [[ "${SANITIZE:-1}" == "1" ]]; then
  run acars_dashboard_model_tests_asan -fsanitize=address,undefined -fno-sanitize-recover=all -I"$UI" \
    "$ROOT/tests/acars_dashboard_model_tests.cpp" "${MODEL[@]}"
fi
RENDER=("$ROOT/tests/acars_dashboard_render_tests.cpp" "${MODEL[@]}" "$UI/acars_dashboard.cpp" "$UI/acars_app.cpp" "$UI/acars_archive.cpp" "$UI/acars_receiver.cpp" "$UI/acars_core.cpp"
  "$UI/focus_nav.cpp" "$UI/screen_controller.cpp" "$ROOT/tests/acars_ui_stubs/audio_header_stub.cpp")
run acars_dashboard_render_tests -I"$ROOT/tests/acars_ui_stubs" -I"$UI" "${RENDER[@]}"
if [[ "${SANITIZE:-1}" == "1" ]]; then
  run acars_dashboard_render_tests_asan -fsanitize=address,undefined -fno-sanitize-recover=all \
    -I"$ROOT/tests/acars_ui_stubs" -I"$UI" "${RENDER[@]}"
fi
ARCHIVE=("$ROOT/tests/acars_archive_tests.cpp" "$UI/acars_archive.cpp" "${MODEL[@]}")
run acars_archive_tests -I"$UI" "${ARCHIVE[@]}"
if [[ "${SANITIZE:-1}" == "1" ]]; then
  run acars_archive_tests_asan -fsanitize=address,undefined -fno-sanitize-recover=all -I"$UI" "${ARCHIVE[@]}"
fi
DIRTY=("$ROOT/tests/acars_dirty_rects_tests.cpp" "$UI/acars_dirty_rects.cpp")
run acars_dirty_rects_tests -I"$UI" "${DIRTY[@]}"
SELFTEST=("$ROOT/tests/acars_selftest_tests.cpp" "$UI/acars_core.cpp" "$UI/acars_selftest.cpp")
run acars_selftest_tests -I"$UI" "${SELFTEST[@]}"
RECEIVER=("$ROOT/tests/acars_receiver_tests.cpp" "$UI/acars_core.cpp" "$UI/acars_receiver.cpp" "${MODEL[@]}")
run acars_receiver_tests -I"$UI" "${RECEIVER[@]}"
if [[ "${SANITIZE:-1}" == "1" ]]; then
  run acars_receiver_tests_asan -fsanitize=address,undefined -fno-sanitize-recover=all -I"$UI" "${RECEIVER[@]}"
fi
echo "ACARS UI host tests passed (log: build/acars/ui-tests.log)"
