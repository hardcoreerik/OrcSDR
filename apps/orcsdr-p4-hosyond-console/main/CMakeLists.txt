# The screens are the Tab5 UI sources compiled unchanged from apps/orcsdr-tab5/ui; the M5Unified
# shim (components/orcsdr_gfx_compat) lets them run on this board. Do not add the real m5unified.
set(WS4B "${CMAKE_CURRENT_LIST_DIR}")
set(TAB5_UI "${CMAKE_CURRENT_LIST_DIR}/../../orcsdr-tab5/ui")
include("${TAB5_UI}/ft8_runtime_sources.cmake")
# Optional ESP-Hosted 3.0.6 image for the kit's ESP32-C6, offered by Settings > Connectivity as an on-device
# update. Pass -DC6_FIRMWARE_BIN=<path to esp_hosted_tab5_c6.bin> (the file name is fixed: it sets the embedded
# symbol names). Without it the update entry is simply not offered.
set(ORCSDR_EMBED_FILES "${TAB5_UI}/../main/orc_badge_104.rgb565")
set(ORCSDR_C6_EMBEDDED 0)
if(C6_FIRMWARE_BIN AND EXISTS "${C6_FIRMWARE_BIN}")
    get_filename_component(C6_FIRMWARE_NAME "${C6_FIRMWARE_BIN}" NAME)
    if(NOT C6_FIRMWARE_NAME STREQUAL "esp_hosted_tab5_c6.bin")
        message(FATAL_ERROR "C6_FIRMWARE_BIN must be named esp_hosted_tab5_c6.bin")
    endif()
    list(APPEND ORCSDR_EMBED_FILES "${C6_FIRMWARE_BIN}")
    set(ORCSDR_C6_EMBEDDED 1)
endif()
idf_component_register(
    # Existing authenticated web modules; browser bundle is packaged separately.
    SRCS "main.cpp" "present.cpp" "screen_snap.cpp" "serial_cli.cpp" "home_app.cpp" "radio_service.cpp" "audio_service.cpp" "screens.cpp" "screen_fm.cpp" "screen_am.cpp" "screen_cb.cpp" "screen_airband.cpp" "screen_shortwave.cpp" "screen_rflab.cpp" "screen_settings.cpp" "screen_pocsag.cpp" "screen_wifi.cpp" "screen_adsb.cpp" "screen_acars.cpp" "screen_lora.cpp" "ui_support.cpp" "iq_tap.cpp" "wifi_app.cpp" "sd_card.cpp"
         ${ORCSDR_FT8_RUNTIME_SOURCES}
         "${TAB5_UI}/home_dashboard.cpp" "${TAB5_UI}/dashboard_audio_control.cpp"
         "${TAB5_UI}/freq_keypad.cpp" "${TAB5_UI}/focus_nav.cpp" "${TAB5_UI}/waterfall_style.cpp"
         "${TAB5_UI}/band_profile.cpp" "${TAB5_UI}/home_vfo.cpp" "${TAB5_UI}/filter_standards.cpp"
         "${TAB5_UI}/dashboard_registry.cpp"
         "${TAB5_UI}/cb_dashboard.cpp" "${TAB5_UI}/cb_scanner.cpp"
         "${TAB5_UI}/airband_dashboard.cpp" "${TAB5_UI}/airband_runtime.cpp" "${TAB5_UI}/airband_scanner.cpp"
         "${TAB5_UI}/airband_catalog.cpp" "${TAB5_UI}/receiver_tuning_controls.cpp"
         "${TAB5_UI}/shortwave_dashboard.cpp" "${TAB5_UI}/shortwave_dashboard_state.cpp" "${TAB5_UI}/shortwave_model.cpp"
         "${TAB5_UI}/shortwave_audio_dsp.cpp" "${TAB5_UI}/shortwave_hunt.cpp" "${TAB5_UI}/shortwave_library.cpp"
         "${TAB5_UI}/orcsdr_storage.cpp" "${TAB5_UI}/wifi_service.cpp" "${TAB5_UI}/orcsdr_restart.cpp"
         "${TAB5_UI}/map_pipeline.cpp"
         "${TAB5_UI}/map_inventory.cpp"
         "${TAB5_UI}/map_service.cpp"
         "${TAB5_UI}/map_preferences.cpp"
         "${TAB5_UI}/map_download_notice.cpp"
         "${TAB5_UI}/map_catalog.cpp"
         "${TAB5_UI}/map_transfer.cpp"
         "${TAB5_UI}/map_activation.cpp"
         "${TAB5_UI}/map_delivery_state.cpp"
         "${TAB5_UI}/map_delivery_nvs.cpp"
         "${TAB5_UI}/map_catalog_session.cpp"
         "${TAB5_UI}/map_install.cpp"
         "${TAB5_UI}/map_downloads.cpp"
         "${TAB5_UI}/map_catalog_fetch.cpp"
         "${TAB5_UI}/map_http_stream.cpp"
         "${TAB5_UI}/acars_interpret.cpp"
         "${TAB5_UI}/vdl2_burst.cpp"
         "${TAB5_UI}/vdl2_channel.cpp"
         "${TAB5_UI}/vdl2_frames.cpp"
         "${TAB5_UI}/vdl2_receiver.cpp"
         "${TAB5_UI}/vdl2_rs.cpp"
         "${TAB5_UI}/map_view.cpp" "${TAB5_UI}/screen_controller.cpp"
         "${TAB5_UI}/lora_dashboard.cpp" "${TAB5_UI}/lora_channel_control.cpp" "${TAB5_UI}/lora_native_decoder.cpp"
         "${TAB5_UI}/lora_preamble_detector.cpp"
         "${TAB5_UI}/acars_app.cpp" "${TAB5_UI}/acars_archive.cpp" "${TAB5_UI}/acars_core.cpp"
         "${TAB5_UI}/acars_dashboard.cpp" "${TAB5_UI}/acars_dashboard_layout.cpp" "${TAB5_UI}/acars_dashboard_model.cpp"
         "${TAB5_UI}/acars_dashboard_ui.cpp" "${TAB5_UI}/acars_demo_data.cpp" "${TAB5_UI}/acars_dirty_rects.cpp"
         "${TAB5_UI}/acars_receiver.cpp"
         "${TAB5_UI}/adsb_dashboard.cpp" "${TAB5_UI}/adsb_decoder.cpp" "${TAB5_UI}/rf24_dashboard.cpp" "${TAB5_UI}/pocsag_dashboard.cpp" "${TAB5_UI}/pocsag_decoder_core.cpp" "${TAB5_UI}/pocsag_store.cpp"
         "${TAB5_UI}/settings_app.cpp" "${TAB5_UI}/receiver_location.cpp" "${TAB5_UI}/clock_settings.cpp" "${TAB5_UI}/rf_lab.cpp" "${TAB5_UI}/rf_analysis.cpp" "${TAB5_UI}/scan_engine.cpp" "${TAB5_UI}/text_editor.cpp" "${TAB5_UI}/airband_catalog_storage.cpp"
         "${TAB5_UI}/am_dashboard.cpp" "${TAB5_UI}/nvs_store.cpp"
         "${TAB5_UI}/fm_dashboard.cpp" "${TAB5_UI}/fm_config.cpp"
    INCLUDE_DIRS "." "${WS4B}" "${TAB5_UI}"
    REQUIRES orcsdr_gnss orcsdr_diagnostics orcsdr_sx126x orcsdr_c6_rom_update esp_rtl_sdr esp_psram esp_timer esp_driver_uart esp_app_format esp_rom mbedtls heap nvs_flash
             json orcmaps esp_http_client esp_https_ota app_update esp_http_server lwip esp_driver_jpeg orcsdr_board_hosyond orcsdr_gfx_compat orcsdr_audio_dsp orcsdr_orientation espressif__esp-dsp espressif__esp_hosted espressif__esp_wifi_remote esp_wifi esp_netif esp_event fatfs sdmmc esp_driver_gptimer esp_driver_sdmmc esp_driver_gpio vfs esp_mm
    EMBED_FILES ${ORCSDR_EMBED_FILES}
)
# Sources that name the drawing target as lgfx::LovyanGFX* (see the M5Unified.h shim).
set_source_files_properties(
    "${TAB5_UI}/acars_dashboard.cpp" "${TAB5_UI}/dead_air_dashboard.cpp" "${TAB5_UI}/ft8_dashboard.cpp"
    PROPERTIES COMPILE_DEFINITIONS "ORCSDR_LOVYANGFX_IS_SCALED;ORCSDR_NO_RAW_BUFFERS")
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
target_sources(${COMPONENT_LIB} PRIVATE "web_auth.cpp" "web_control.cpp" "web_server.cpp")
target_sources(${COMPONENT_LIB} PRIVATE "packet_radio_service.cpp")
target_sources(${COMPONENT_LIB} PRIVATE "gnss_service.cpp")
target_sources(${COMPONENT_LIB} PRIVATE "network_clock.cpp" "${TAB5_UI}/clock_sync_status.cpp")
target_sources(${COMPONENT_LIB} PRIVATE "orientation_app.cpp")
target_compile_options(${COMPONENT_LIB} PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:-include;orcsdr_p4_time.hpp>" "-DORCSDR_HAS_EMBEDDED_C6_FIRMWARE=${ORCSDR_C6_EMBEDDED}")

# Complete ROM recovery is distinct from the legacy Hosted application-only OTA.
target_sources(${COMPONENT_LIB} PRIVATE "c6_rom_package.cpp")
target_sources(${COMPONENT_LIB} PRIVATE "c6_recovery_app.cpp")
if(C6_ROM_PACKAGE_MANIFEST)
    if(NOT CONFIG_ORCSDR_BOARD_LILYGO_T_DISPLAY_P4_AMOLED)
        message(FATAL_ERROR "ROM recovery package is currently qualified only for LilyGO C6")
    endif()
    set(package_generator "${CMAKE_CURRENT_LIST_DIR}/../../../tools/release/embed-c6-rom-package.py")
    set(package_dir "${CMAKE_CURRENT_BINARY_DIR}/c6_rom_package")
    execute_process(COMMAND "${PYTHON}" "${package_generator}"
        --manifest "${C6_ROM_PACKAGE_MANIFEST}"
        --expected-manifest-sha256 "${C6_ROM_PACKAGE_SHA256}" --output "${package_dir}"
        RESULT_VARIABLE package_result ERROR_VARIABLE package_error)
    if(NOT package_result EQUAL 0)
        message(FATAL_ERROR "C6 recovery package validation failed: ${package_error}")
    endif()
    include("${package_dir}/c6_rom_files.cmake")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${C6_ROM_PACKAGE_MANIFEST}" "${package_generator}" ${ORCSDR_C6_ROM_FILES}
        "${CMAKE_CURRENT_LIST_DIR}/../../../tools/release/hosted-c6-release.json"
        "${CMAKE_CURRENT_LIST_DIR}/../../../orcdial/c6/CMakeLists.txt"
        "${CMAKE_CURRENT_LIST_DIR}/../../../orcdial/c6/main/CMakeLists.txt"
        "${CMAKE_CURRENT_LIST_DIR}/../../../orcdial/c6/main/main.c"
        "${CMAKE_CURRENT_LIST_DIR}/../../../orcdial/c6/sdkconfig.defaults"
        "${CMAKE_CURRENT_LIST_DIR}/../../../orcdial/c6/partitions_eh_cp_ota_4m.csv")
    target_sources(${COMPONENT_LIB} PRIVATE "${package_dir}/c6_rom_manifest.cpp")
    foreach(image IN LISTS ORCSDR_C6_ROM_FILES)
        target_add_binary_data(${COMPONENT_LIB} "${image}" BINARY)
    endforeach()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ORCSDR_HAS_C6_ROM_PACKAGE=1)
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ORCSDR_HAS_C6_ROM_PACKAGE=0)
endif()
