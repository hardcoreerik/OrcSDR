#!/bin/sh
# Runs inside the emscripten/emsdk container. /repo is the repository root.
# Output: /repo/apps/orcsdr-web/web/dist/core.js + core.wasm
set -e
R=/repo
W=$R/apps/orcsdr-web/web
G=$W/third_party/M5GFX
L=$G/src/lgfx/v1
OUT=$W/dist
mkdir -p "$OUT" "$OUT/gen"

# The dashboard and decoder sources are the ones the devices compile (apps/orcsdr-tab5/ui). Three headers there (nvs_store,
# orcsdr_storage, scan_engine) have browser stand-ins in stubs/; a quoted include finds the file next to its includer first,
# so the stand-ins can only win if they sit beside the sources. The build therefore lays the device sources plus those stand-ins
# into a throwaway directory under dist/ (not committed) and compiles from there. No other file differs from the device sources.
UI=$OUT/gen/ui
rm -rf "$UI" && mkdir -p "$UI"
cp "$R"/apps/orcsdr-tab5/ui/* "$UI"/
for f in "$W"/stubs/*; do
  [ -f "$f" ] && [ -e "$UI/$(basename "$f")" ] && cp "$f" "$UI/"
done

# The badge is linked into the device image from a .rgb565 file; embed the same bytes here.
python3 - "$W/orc_badge_104.rgb565" "$OUT/gen/badge_data.cpp" <<'PY'
import sys
data = open(sys.argv[1], 'rb').read()
with open(sys.argv[2], 'w') as f:
    f.write('#include <stdint.h>\nextern "C" {\nextern const uint8_t _binary_orc_badge_104_rgb565_start[] = {')
    f.write(','.join(str(b) for b in data))
    f.write('};\n}\n')
PY

emcc -O2 -std=c++17 -D__linux__ -DLGFX_LINUX_FB -Wno-everything \
  -I"$UI" -I"$W/stubs" -I"$W" -I"$G/src" \
  "$W/web_main.cpp" "$W/web_platform.cpp" "$W/data_screens.cpp" "$W/pocsag_controller.cpp" "$W/adsb_controller.cpp" "$W/acars_controller.cpp" "$W/p25_controller.cpp" "$W/ft8_controller.cpp" "$W/rf24_controller.cpp" "$W/rflab_controller.cpp" "$W/vizctl_controller.cpp" "$W/deadair_controller.cpp" "$W/settings_controller.cpp" "$W/lora_controller.cpp" "$UI/lora_dashboard.cpp" "$UI/lora_channel_control.cpp" "$UI/lora_history.cpp" "$UI/lora_packet_log.cpp" "$UI/settings_app.cpp" "$UI/clock_settings.cpp" "$UI/dead_air_app.cpp" "$UI/dead_air_audio.cpp" "$UI/dead_air_clip.cpp" "$UI/dead_air_dashboard.cpp" "$UI/dead_air_format.cpp" "$UI/dead_air_info.cpp" "$UI/dead_air_journal.cpp" "$UI/dead_air_model.cpp" "$UI/dead_air_palette.cpp" "$UI/dead_air_sd_journal.cpp" "$UI/dead_air_sweep.cpp" "$UI/screen_controller.cpp" "$UI/rf_visualizer.cpp" "$UI/rf_visualizer_controls.cpp" "$W/rf_analysis_web.cpp" "$UI/rf_lab.cpp" "$UI/rf24_dashboard.cpp" "$W/sstv_controller.cpp" "$W/sstv_web_support.cpp" "$OUT/gen/badge_data.cpp" \
  "$UI/home_dashboard.cpp" "$UI/focus_nav.cpp" "$UI/waterfall_style.cpp" \
  "$UI/freq_keypad.cpp" "$UI/dashboard_audio_control.cpp" \
  "$UI/dashboard_registry.cpp" "$UI/filter_standards.cpp" "$UI/fm_dashboard.cpp" "$UI/am_dashboard.cpp" \
  "$UI/cb_dashboard.cpp" "$UI/cb_scanner.cpp" "$W/cb_controller.cpp" \
  "$UI/shortwave_dashboard.cpp" "$UI/shortwave_dashboard_state.cpp" "$UI/shortwave_audio_dsp.cpp" \
  "$UI/shortwave_library.cpp" "$UI/shortwave_library_io.cpp" "$UI/shortwave_model.cpp" \
  "$UI/shortwave_ssb.cpp" "$UI/receiver_tuning_controls.cpp" "$UI/text_editor.cpp" \
  "$UI/airband_dashboard.cpp" "$UI/airband_runtime.cpp" "$UI/airband_scanner.cpp" \
  "$UI/airband_catalog.cpp" "$UI/airband_catalog_storage.cpp" "$UI/airband_audio_filter.cpp" \
  "$UI/atc_presets.cpp" "$UI/band_profile.cpp" "$W/ab_controller.cpp" \
  "$UI/sstv_dashboard.cpp" "$UI/sstv_status.cpp" "$UI/sstv_tune.cpp" "$UI/sstv_audio_scope.cpp" "$UI/sstv_settings.cpp" "$UI/sstv_receiver.cpp" "$UI/sstv_modes.cpp" "$UI/sstv_auto_detect.cpp" "$UI/ft8_dashboard.cpp" "$UI/ft8_hunter.cpp" "$UI/ft8_world_data.cpp" "$UI/ft8_conditions.cpp" "$UI/ft8_info_text.cpp" "$UI/ft8_tuning.cpp" "$UI/clock_sync_status.cpp" "$UI/ft8_heard_db.cpp" "$UI/ft8_model.cpp" "$UI/p25_dashboard.cpp" "$UI/p25_config.cpp" "$UI/p25_decoder_core.cpp" "$UI/acars_dashboard.cpp" "$UI/acars_dashboard_layout.cpp" "$UI/acars_dashboard_model.cpp" "$UI/acars_dashboard_ui.cpp" "$UI/acars_demo_data.cpp" "$UI/acars_dirty_rects.cpp" "$UI/acars_archive.cpp" "$UI/acars_core.cpp" "$UI/adsb_dashboard.cpp" "$UI/offline_map.cpp" "$UI/pocsag_dashboard.cpp" "$UI/pocsag_store.cpp" "$UI/keyboard_input.cpp" "$W/shortwave_hunt_web.cpp" "$W/sw_controller.cpp" "$W/web_storage.cpp" \
  $L/LGFXBase.cpp $L/LGFX_Sprite.cpp $L/lgfx_fonts.cpp \
  $L/misc/SpriteBuffer.cpp $L/misc/pixelcopy.cpp $L/misc/common_function.cpp \
  -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=33554432 -sMODULARIZE=1 -sEXPORT_NAME=M5Core \
  -sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAPF32,UTF8ToString,stringToUTF8,lengthBytesUTF8 \
  -sEXPORTED_FUNCTIONS=_malloc,_free \
  -o "$OUT/core.js"
ls -l "$OUT"
