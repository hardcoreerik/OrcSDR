# The screens are the Tab5 UI sources compiled unchanged from apps/orcsdr-tab5/ui; the M5Unified
# shim (components/orcsdr_gfx_compat) lets them run on this board. Do not add the real m5unified.
set(WS4B "${CMAKE_CURRENT_LIST_DIR}/../../orcsdr-waveshare-4b/main")
set(TAB5_UI "${CMAKE_CURRENT_LIST_DIR}/../../orcsdr-tab5/ui")
idf_component_register(
    SRCS "main.cpp" "present.cpp" "screen_snap.cpp" "serial_cli.cpp" "home_app.cpp" "radio_service.cpp" "audio_service.cpp" "screens.cpp" "screen_fm.cpp" "screen_am.cpp" "screen_cb.cpp" "screen_airband.cpp" "screen_shortwave.cpp" "p4_storage_stub.cpp"
         "${TAB5_UI}/home_dashboard.cpp" "${TAB5_UI}/dashboard_audio_control.cpp"
         "${TAB5_UI}/freq_keypad.cpp" "${TAB5_UI}/focus_nav.cpp" "${TAB5_UI}/waterfall_style.cpp"
         "${TAB5_UI}/band_profile.cpp" "${TAB5_UI}/home_vfo.cpp" "${TAB5_UI}/filter_standards.cpp"
         "${TAB5_UI}/dashboard_registry.cpp"
         "${TAB5_UI}/cb_dashboard.cpp" "${TAB5_UI}/cb_scanner.cpp"
         "${TAB5_UI}/airband_dashboard.cpp" "${TAB5_UI}/airband_runtime.cpp" "${TAB5_UI}/airband_scanner.cpp"
         "${TAB5_UI}/airband_catalog.cpp" "${TAB5_UI}/receiver_tuning_controls.cpp"
         "${TAB5_UI}/shortwave_dashboard.cpp" "${TAB5_UI}/shortwave_dashboard_state.cpp" "${TAB5_UI}/shortwave_model.cpp"
         "${TAB5_UI}/shortwave_audio_dsp.cpp" "${TAB5_UI}/shortwave_hunt.cpp" "${TAB5_UI}/shortwave_library.cpp"
         "${TAB5_UI}/scan_engine.cpp" "${TAB5_UI}/text_editor.cpp" "${TAB5_UI}/airband_catalog_storage.cpp"
         "${TAB5_UI}/am_dashboard.cpp" "${TAB5_UI}/nvs_store.cpp"
         "${TAB5_UI}/fm_dashboard.cpp" "${TAB5_UI}/fm_config.cpp"
    INCLUDE_DIRS "." "${WS4B}" "${TAB5_UI}"
    REQUIRES esp_rtl_sdr esp_psram esp_timer esp_driver_uart esp_app_format esp_rom mbedtls heap nvs_flash
             orcsdr_board_hosyond orcsdr_gfx_compat orcsdr_audio_dsp
    EMBED_FILES "${WS4B}/orc_badge_104.rgb565"
)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:-include;orcsdr_p4_time.hpp>")
