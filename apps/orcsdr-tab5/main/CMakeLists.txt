set(ORCSDR_EMBED_FILES
    "orc_badge_104.png"
    "orc_badge_104.rgb565"
    "../ui/web_console.html")
set(ORCSDR_LDFRAGMENTS)
if(C6_FIRMWARE_BIN)
    get_filename_component(C6_FIRMWARE_BIN "${C6_FIRMWARE_BIN}" ABSOLUTE)
    if(NOT EXISTS "${C6_FIRMWARE_BIN}")
        message(FATAL_ERROR "C6_FIRMWARE_BIN does not exist: ${C6_FIRMWARE_BIN}")
    endif()
    get_filename_component(C6_FIRMWARE_NAME "${C6_FIRMWARE_BIN}" NAME)
    if(NOT C6_FIRMWARE_NAME STREQUAL "esp_hosted_tab5_c6.bin")
        message(FATAL_ERROR "C6_FIRMWARE_BIN must be named esp_hosted_tab5_c6.bin for stable embedded symbols.")
    endif()
    list(APPEND ORCSDR_EMBED_FILES "${C6_FIRMWARE_BIN}")
    list(APPEND ORCSDR_LDFRAGMENTS "linker.lf")
endif()

idf_component_register(
    SRCS
        "../ui/main.cpp"
        "../ui/orcsdr_splash.cpp"
        "../ui/orcsdr_storage.cpp"
        "../ui/adsb_dashboard.cpp"
        "../ui/adsb_decoder.cpp"
        "../ui/atc_presets.cpp"
        "../ui/catalog_sync.cpp"
        "../ui/dashboard_audio_control.cpp"
        "../ui/dashboard_registry.cpp"
        "../ui/device_status_service.cpp"
        "../ui/screen_controller.cpp"
        "../ui/scan_engine.cpp"
        "../ui/fm_dashboard.cpp"
        "../ui/fm_config.cpp"
        "../ui/home_dashboard.cpp"
        "../ui/lora_dashboard.cpp"
        "../ui/lora_native_decoder.cpp"
        "../ui/location_estimate.cpp"
        "../ui/offline_map.cpp"
        "../ui/navigation_service.cpp"
        "../ui/nvs_store.cpp"
        "../ui/p25_dashboard.cpp"
        "../ui/p25_config.cpp"
        "../ui/p25_decoder.cpp"
        "../ui/radio_session.cpp"
        "../ui/radio_ui_service.cpp"
        "../ui/rf_analysis.cpp"
        "../ui/rf_lab.cpp"
        "../ui/rf_visualizer.cpp"
        "../ui/rf_visualizer_controls.cpp"
        "../ui/settings_app.cpp"
        "../ui/text_editor.cpp"
        "../ui/ui_capture.cpp"
        "../ui/web_console.cpp"
        "../ui/rf24_dashboard.cpp"
        "../ui/wifi_service.cpp"
    INCLUDE_DIRS "." "../ui"
    REQUIRES
        m5unified
        m5gfx
        mbelib
        nvs_flash
        esp_rtl_sdr
        esp_driver_usb_serial_jtag
        esp_driver_jpeg
        fatfs
        esp_hosted
        esp_event
        esp_netif
        esp_wifi
        esp-dsp
        esp_hw_support
        esp_psram
        esp_system
        espcoredump
        esp_app_format
        esp_timer
        sdmmc
        vfs
        esp_http_client
        esp_http_server
        mdns
        esp-tls
        json
        spi_flash
        usb
    EMBED_FILES ${ORCSDR_EMBED_FILES}
    LDFRAGMENTS ${ORCSDR_LDFRAGMENTS}
    EMBED_TXTFILES
        "catalog_public_key.pem"
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    ORCSDR_HAS_EMBEDDED_C6_FIRMWARE=$<BOOL:${C6_FIRMWARE_BIN}>)
target_compile_options(${COMPONENT_LIB} PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:-include;orcsdr_m5_time.hpp>")
