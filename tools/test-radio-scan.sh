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

airband_sources=(tests/airband_scanner_tests.cpp apps/orcsdr-tab5/ui/airband_scanner.cpp)
g++ "${common[@]}" -O2 "${airband_sources[@]}" -o "$build_dir/airband_scanner_tests"
"$build_dir/airband_scanner_tests"
g++ "${common[@]}" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  "${airband_sources[@]}" -o "$build_dir/airband_scanner_tests_sanitized"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  "$build_dir/airband_scanner_tests_sanitized"

airband_catalog_sources=(tests/airband_catalog_tests.cpp apps/orcsdr-tab5/ui/airband_catalog.cpp apps/orcsdr-tab5/ui/airband_scanner.cpp)
g++ "${common[@]}" -O2 "${airband_catalog_sources[@]}" -o "$build_dir/airband_catalog_tests"
"$build_dir/airband_catalog_tests"
g++ "${common[@]}" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  "${airband_catalog_sources[@]}" -o "$build_dir/airband_catalog_tests_sanitized"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  "$build_dir/airband_catalog_tests_sanitized"

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

filter_standard_sources=(tests/filter_standards_tests.cpp apps/orcsdr-tab5/ui/filter_standards.cpp)
g++ "${common[@]}" -O2 "${filter_standard_sources[@]}" -o "$build_dir/filter_standards_tests"
"$build_dir/filter_standards_tests"
g++ "${common[@]}" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  "${filter_standard_sources[@]}" -o "$build_dir/filter_standards_tests_sanitized"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  "$build_dir/filter_standards_tests_sanitized"

waterfall_style_sources=(tests/waterfall_style_tests.cpp apps/orcsdr-tab5/ui/waterfall_style.cpp)
g++ "${common[@]}" -O2 "${waterfall_style_sources[@]}" -o "$build_dir/waterfall_style_tests"
"$build_dir/waterfall_style_tests"
g++ "${common[@]}" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer   "${waterfall_style_sources[@]}" -o "$build_dir/waterfall_style_tests_sanitized"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1   "$build_dir/waterfall_style_tests_sanitized"

airband_audio_sources=(tests/airband_audio_filter_tests.cpp apps/orcsdr-tab5/ui/airband_audio_filter.cpp)
g++ "${common[@]}" -O2 "${airband_audio_sources[@]}" -o "$build_dir/airband_audio_filter_tests"
"$build_dir/airband_audio_filter_tests"
g++ "${common[@]}" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer   "${airband_audio_sources[@]}" -o "$build_dir/airband_audio_filter_tests_sanitized"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1   "$build_dir/airband_audio_filter_tests_sanitized"

weather_model_sources=(tests/weather_model_tests.cpp apps/orcsdr-tab5/ui/weather_model.cpp)
g++ "${common[@]}" -O2 "${weather_model_sources[@]}" -o "$build_dir/weather_model_tests"
"$build_dir/weather_model_tests"
g++ "${common[@]}" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  "${weather_model_sources[@]}" -o "$build_dir/weather_model_tests_sanitized"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  "$build_dir/weather_model_tests_sanitized"

weather_noaa_sources=(tests/weather_noaa_tests.cpp apps/orcsdr-tab5/ui/weather_noaa.cpp)
g++ "${common[@]}" -O2 "${weather_noaa_sources[@]}" -o "$build_dir/weather_noaa_tests"
"$build_dir/weather_noaa_tests"
g++ "${common[@]}" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  "${weather_noaa_sources[@]}" -o "$build_dir/weather_noaa_tests_sanitized"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  "$build_dir/weather_noaa_tests_sanitized"

weather_report_sources=(tests/weather_report_format_tests.cpp apps/orcsdr-tab5/ui/weather_model.cpp apps/orcsdr-tab5/ui/weather_report_format.cpp)
g++ "${common[@]}" -O2 "${weather_report_sources[@]}" -o "$build_dir/weather_report_format_tests"
"$build_dir/weather_report_format_tests"
g++ "${common[@]}" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  "${weather_report_sources[@]}" -o "$build_dir/weather_report_format_tests_sanitized"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  "$build_dir/weather_report_format_tests_sanitized"

weather_screen_sources=(tests/weather_screen_tests.cpp apps/orcsdr-tab5/ui/screen_controller.cpp)
g++ "${common[@]}" -O2 "${weather_screen_sources[@]}" -o "$build_dir/weather_screen_tests"
"$build_dir/weather_screen_tests"
g++ "${common[@]}" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  "${weather_screen_sources[@]}" -o "$build_dir/weather_screen_tests_sanitized"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  "$build_dir/weather_screen_tests_sanitized"

weather_service_sources=(tests/weather_service_tests.cpp apps/orcsdr-tab5/ui/weather_noaa.cpp apps/orcsdr-tab5/ui/weather_service.cpp)
g++ "${common[@]}" -O2 "${weather_service_sources[@]}" -o "$build_dir/weather_service_tests"
"$build_dir/weather_service_tests"
g++ "${common[@]}" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  "${weather_service_sources[@]}" -o "$build_dir/weather_service_tests_sanitized"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  "$build_dir/weather_service_tests_sanitized"


weather_runtime_sources=(tests/weather_runtime_tests.cpp apps/orcsdr-tab5/ui/weather_noaa.cpp apps/orcsdr-tab5/ui/weather_service.cpp apps/orcsdr-tab5/ui/weather_runtime.cpp)
g++ "${common[@]}" -O2 "${weather_runtime_sources[@]}" -o "$build_dir/weather_runtime_tests"
"$build_dir/weather_runtime_tests"
g++ "${common[@]}" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  "${weather_runtime_sources[@]}" -o "$build_dir/weather_runtime_tests_sanitized"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  "$build_dir/weather_runtime_tests_sanitized"
