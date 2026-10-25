# The screens are the Tab5 UI sources compiled unchanged from apps/orcsdr-tab5/ui; the M5Unified
# shim (components/orcsdr_gfx_compat) lets them run on this board. Do not add the real m5unified.
set(WS4B "${CMAKE_CURRENT_LIST_DIR}/../../orcsdr-waveshare-4b/main")
set(TAB5_UI "${CMAKE_CURRENT_LIST_DIR}/../../orcsdr-tab5/ui")
idf_component_register(
    SRCS "main.cpp" "present.cpp" "screen_snap.cpp" "home_app.cpp" "radio_service.cpp"
         "${TAB5_UI}/home_dashboard.cpp" "${TAB5_UI}/dashboard_audio_control.cpp"
         "${TAB5_UI}/freq_keypad.cpp" "${TAB5_UI}/focus_nav.cpp" "${TAB5_UI}/waterfall_style.cpp"
         "${TAB5_UI}/band_profile.cpp" "${TAB5_UI}/home_vfo.cpp" "${TAB5_UI}/filter_standards.cpp"
         "${TAB5_UI}/dashboard_registry.cpp"
    INCLUDE_DIRS "." "${WS4B}" "${TAB5_UI}"
    REQUIRES esp_rtl_sdr esp_psram esp_timer esp_driver_uart esp_app_format esp_rom mbedtls heap
             orcsdr_board_hosyond orcsdr_gfx_compat
    EMBED_FILES "${WS4B}/orc_badge_104.rgb565"
)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:-include;orcsdr_p4_time.hpp>")
