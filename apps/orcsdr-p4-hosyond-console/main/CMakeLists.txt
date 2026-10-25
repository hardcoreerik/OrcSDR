# Shared pieces compiled unchanged from their original directories:
#  - the adaptive Home binding, serial PNG screenshot service and badge from the
#    Waveshare 4B app (recovered from closed PR #197, commit b7660c58);
#  - the waterfall palette from the Tab5 app (hardware independent).
set(WS4B "${CMAKE_CURRENT_LIST_DIR}/../../orcsdr-waveshare-4b/main")
set(TAB5_UI "${CMAKE_CURRENT_LIST_DIR}/../../orcsdr-tab5/ui")
idf_component_register(
    SRCS "main.cpp" "lvgl_port.cpp" "radio_service.cpp"
         "${WS4B}/orcui_4b_adapter.cpp" "${WS4B}/screen_capture.cpp"
         "${TAB5_UI}/waterfall_style.cpp"
    INCLUDE_DIRS "." "compat" "${WS4B}" "${TAB5_UI}"
    REQUIRES lvgl esp_rtl_sdr esp_psram esp_timer esp_driver_uart esp_app_format
             espressif__libpng mbedtls orcui orcsdr_board_hosyond
    EMBED_FILES "${WS4B}/orc_badge_104.rgb565"
)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
