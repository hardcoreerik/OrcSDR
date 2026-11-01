set(SHARED_UI "${CMAKE_CURRENT_LIST_DIR}/../../orcsdr-tab5/ui")
idf_component_register(SRCS "main.cpp" "preview_services.cpp" "${SHARED_UI}/freq_keypad.cpp"
    "${SHARED_UI}/focus_nav.cpp" "${SHARED_UI}/dashboard_audio_control.cpp"
    INCLUDE_DIRS "." "${SHARED_UI}"
    REQUIRES orcsdr_board_hosyond orcsdr_gfx_compat orcsdr_display_geometry
             esp_driver_uart esp_timer esp_mm heap vfs
    EMBED_FILES "${SHARED_UI}/../main/orc_badge_104.rgb565")
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
