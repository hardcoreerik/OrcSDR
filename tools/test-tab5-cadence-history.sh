#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
ui=apps/orcsdr-tab5/ui
flags=(-std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -ffunction-sections -fdata-sections -Wl,--gc-sections -I"$ui")
run() {
  local name=$1; shift
  g++ "${flags[@]}" "$@" -o "$out/$name"
  "$out/$name"
  echo "$name PASS"
}
run home_cadence tests/home_cadence_tests.cpp
run waterfall_style tests/waterfall_style_tests.cpp "$ui/waterfall_style.cpp"
run lora_history tests/lora_history_tests.cpp "$ui/lora_history.cpp" "$ui/lora_packet_log.cpp"
run memory_ledger tests/memory_ledger_tests.cpp "$ui/memory_ledger.cpp"
run serial_help tests/serial_help_tests.cpp "$ui/serial_help.cpp"
run serial_memory tests/serial_memory_tests.cpp "$ui/serial_memory.cpp" "$ui/memory_ledger.cpp" "$ui/memory_reclaim.cpp"

# Focused release-review lifecycle regressions exercise the production routines.
python3 tests/setup_navigation_pause_test.py
python3 tests/location_task_lifetime_test.py
python3 tests/orcdial_init_backoff_test.py
python3 tests/adsb_zoom_redraw_test.py
python3 tests/map_download_scheduling_test.py
python3 tests/map_worker_startup_test.py
python3 tests/storage_update_mode_test.py
python3 tests/lora_log_recovery_test.py
run dsp_stage_stats -Iapps/orcsdr-tab5/dsp tests/dsp_stage_stats_tests.cpp apps/orcsdr-tab5/dsp/dsp_stats.cpp
python3 tests/map_download_task_lifetime_test.py
