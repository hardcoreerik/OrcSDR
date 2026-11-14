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
run buffer_claim -pthread tests/buffer_claim_tests.cpp
run sd_writer -pthread tests/sd_writer_tests.cpp "$ui/sd_writer.cpp"
run rds_capture -pthread tests/rds_capture_tests.cpp "$ui/rds_capture.cpp" "$ui/sd_writer.cpp" "$ui/memory_reclaim.cpp"
run audio_rec -pthread tests/audio_rec_tests.cpp "$ui/audio_rec.cpp" "$ui/sd_writer.cpp" "$ui/memory_reclaim.cpp"
run display_rotate tests/display_rotate_tests.cpp
run serial_display_bench tests/serial_display_bench_tests.cpp "$ui/serial_display_bench.cpp"
run serial_rec tests/serial_rec_tests.cpp "$ui/serial_rec.cpp"
run serial_ui_debug tests/serial_ui_debug_tests.cpp "$ui/serial_ui_debug.cpp"
run serial_lab_vis tests/serial_lab_vis_tests.cpp "$ui/serial_lab_vis.cpp"
run serial_iq tests/serial_iq_tests.cpp "$ui/serial_iq.cpp"
run serial_iq_transfer tests/serial_iq_transfer_tests.cpp "$ui/serial_iq_transfer.cpp"
run serial_ui_action -Iapps/orcsdr-tab5/dsp tests/serial_ui_action_tests.cpp "$ui/serial_ui_action.cpp"
run serial_radio tests/serial_radio_tests.cpp "$ui/serial_radio.cpp"
run serial_small_families tests/serial_small_families_tests.cpp "$ui/serial_digimodes.cpp" "$ui/serial_splash.cpp"
run serial_help tests/serial_help_tests.cpp "$ui/serial_help.cpp"
run serial_memory tests/serial_memory_tests.cpp "$ui/serial_memory.cpp" "$ui/memory_ledger.cpp" "$ui/memory_reclaim.cpp"
run serial_keyboard tests/serial_keyboard_tests.cpp "$ui/serial_keyboard.cpp"
run serial_dead_air tests/serial_dead_air_tests.cpp "$ui/serial_dead_air.cpp"
run serial_dsp tests/serial_dsp_tests.cpp "$ui/serial_dsp.cpp"
run serial_pocsag tests/serial_pocsag_tests.cpp "$ui/serial_pocsag.cpp"
run serial_rds tests/serial_rds_tests.cpp "$ui/serial_rds.cpp"
run serial_p25 tests/serial_p25_tests.cpp "$ui/serial_p25.cpp" "$ui/p25_decoder_core.cpp"
run serial_wifi tests/serial_wifi_tests.cpp "$ui/serial_wifi.cpp"
run serial_catalog tests/serial_catalog_tests.cpp "$ui/serial_catalog.cpp"
run serial_location tests/serial_location_tests.cpp "$ui/serial_location.cpp"
run serial_adsb tests/serial_adsb_tests.cpp "$ui/serial_adsb.cpp"
run serial_wifi_c6_fault -DORCSDR_C6_FAULT_TEST=1 tests/serial_wifi_tests.cpp "$ui/serial_wifi.cpp"
run serial_gain tests/serial_gain_tests.cpp "$ui/serial_gain.cpp"
run serial_driver tests/serial_driver_tests.cpp "$ui/serial_driver.cpp"
run serial_rtc tests/serial_rtc_tests.cpp "$ui/serial_rtc.cpp"
run serial_status tests/serial_status_tests.cpp "$ui/serial_status.cpp"
run serial_audio tests/serial_audio_tests.cpp "$ui/serial_audio.cpp"
run serial_audio_test tests/serial_audio_test_tests.cpp "$ui/serial_audio_test.cpp"
run serial_web tests/serial_web_tests.cpp "$ui/serial_web.cpp"
run serial_tune tests/serial_tune_tests.cpp "$ui/serial_tune.cpp"
run serial_signal tests/serial_signal_tests.cpp "$ui/serial_signal.cpp"
run serial_preset tests/serial_preset_tests.cpp "$ui/serial_preset.cpp"
run serial_scope tests/serial_scope_tests.cpp "$ui/serial_scope.cpp"
run serial_verbosity tests/serial_verbosity_tests.cpp "$ui/serial_verbosity.cpp"
run serial_health tests/serial_health_tests.cpp "$ui/serial_health.cpp"
run serial_lora_status tests/serial_lora_status_tests.cpp "$ui/serial_lora_status.cpp"
run serial_lora tests/serial_lora_tests.cpp "$ui/serial_lora.cpp"
run serial_bench tests/serial_bench_tests.cpp "$ui/serial_bench.cpp"
run serial_decoder_diag tests/serial_decoder_diag_tests.cpp "$ui/serial_decoder_diag.cpp"
run serial_ui_status tests/serial_ui_status_tests.cpp "$ui/serial_ui_status.cpp"
run serial_ui_nav tests/serial_ui_nav_tests.cpp "$ui/serial_ui_nav.cpp"
# The DSP lab and A/B routes exist only in test builds; compile those variants too.
run serial_dsp_lab -DORCSDR_DSP_LAB=1 tests/serial_dsp_tests.cpp "$ui/serial_dsp.cpp"
run serial_dsp_ab -DORCSDR_DSP_AB=1 tests/serial_dsp_tests.cpp "$ui/serial_dsp.cpp"
run serial_rds_ab -DORCSDR_DSP_AB=1 tests/serial_rds_tests.cpp "$ui/serial_rds.cpp"

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
