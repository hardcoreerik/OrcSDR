include("${CMAKE_CURRENT_LIST_DIR}/map_catalog_config.cmake")
set(ORCSDR_EMBED_FILES
    "orc_badge_104.png"
    "orc_badge_104.rgb565"
    "orcsdr_splash_1280x720.jpg"
    "sstv_testpattern_320x240.rgb"
    "../ui/web_console.html"
    "../ui/web_audio.js")
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

# Stage-2 frontend benchmark lab (test builds only; see dsp/frontend_lab.hpp).
set(ORCSDR_DSP_LAB 0 CACHE STRING "1 = Stage-2 multirate frontend benchmark lab (test builds only)")
set(ORCSDR_LAB_SRCS)
if(ORCSDR_DSP_LAB)
    list(APPEND ORCSDR_LAB_SRCS "../dsp/multirate.cpp" "../dsp/frontend_lab.cpp")
endif()

idf_component_register(
    SRCS
        ${ORCSDR_LAB_SRCS}
        "../ui/main.cpp"
        "../ui/orcsdr_splash.cpp"
        "../ui/orcsdr_restart.cpp"
        "../ui/orcsdr_storage.cpp"
        "../ui/am_dashboard.cpp"
        "../ui/ft8_model.cpp"
        "../ui/ft8_conditions.cpp"
        "../ui/ft8_adif.cpp"
        "../ui/ft8_mode.cpp"
        "../ui/ft8_sync.cpp"
        "../ui/ft8_demod.cpp"
        "../ui/ft8_spectral_fft.cpp"
        "../ui/ft8_pipeline.cpp"
        "../ui/ft8_snr.cpp"
        "../ui/js8_mode.cpp"
        "../ui/js8_frame.cpp"
        "../ui/js8_sync.cpp"
        "../ui/js8_spectral.cpp"
        "../ui/js8_demod.cpp"
        "../ui/js8_frontend.cpp"
        "../ui/js8_fec.cpp"
        "../ui/js8_codec.cpp"
        "../ui/js8_message.cpp"
        "../ui/js8_osd.cpp"
        "../ui/js8_decoder.cpp"
        "../ui/js8_snr.cpp"
        "../ui/js8_native_backend.cpp"
        "../ui/ft8_message.cpp"
        "../ui/ft8_codec.cpp"
        "../ui/ft8_ldpc.cpp"
        "../ui/ft8_ldpc_decode.cpp"
        "../ui/ft8_audio_tap.cpp"
        "../ui/ft8_native_backend.cpp"
        "../ui/ft8_runtime.cpp"
        "../ui/ft8_clock_estimator.cpp"
        "../ui/wwv_tick.cpp"
        "../ui/memory_reclaim.cpp"
        "../ui/ft8_hunter.cpp"
        "../ui/ft8_dashboard.cpp"
        "../ui/ft8_map_source.cpp"
        "../ui/sstv_receiver.cpp"
        "../ui/sstv_modes.cpp"
        "../ui/sstv_auto_detect.cpp"
        "../ui/sstv_archive.cpp"
        "../ui/sstv_status.cpp"
        "../ui/sstv_tune.cpp"
        "../ui/sstv_audio_scope.cpp"
        "../ui/sstv_capture.cpp"
        "../ui/sstv_export.cpp"
        "../ui/sstv_settings.cpp"
        "../ui/sstv_app.cpp"
        "../ui/sstv_diag.cpp"
        "../ui/sstv_dashboard.cpp"
        "../ui/ft8_tuning.cpp"
        "../ui/ft8_info_text.cpp"
        "../ui/ft8_heard_db.cpp"
        "../ui/am_finder.cpp"
        "../ui/shortwave_model.cpp"
        "../ui/shortwave_audio_dsp.cpp"
        "../ui/shortwave_library.cpp"
        "../ui/shortwave_library_io.cpp"
        "../ui/shortwave_hunt.cpp"
        "../ui/shortwave_dashboard.cpp"
        "../ui/shortwave_dashboard_state.cpp"
        "../ui/shortwave_ssb.cpp"
        "../ui/receiver_tuning_controls.cpp"
        "../ui/adsb_dashboard.cpp"
        "../ui/adsb_decoder.cpp"
        "../ui/atc_presets.cpp"
        "../ui/airband_scanner.cpp"
        "../ui/airband_catalog.cpp"
        "../ui/airband_catalog_storage.cpp"
        "../ui/airband_dashboard.cpp"
        "../ui/airband_runtime.cpp"
        "../ui/catalog_sync.cpp"
        "../ui/map_catalog.cpp"
        "../ui/map_transfer.cpp"
        "../ui/map_activation.cpp"
        "../ui/map_delivery_state.cpp"
        "../ui/map_delivery_nvs.cpp"
        "../ui/map_catalog_session.cpp"
        "../ui/map_install.cpp"
        "../ui/map_downloads.cpp"
        "../ui/map_catalog_fetch.cpp"
        "../ui/map_http_stream.cpp"
        "../ui/cb_dashboard.cpp"
        "../ui/cb_scanner.cpp"
        "../ui/dashboard_audio_control.cpp"
        "../ui/dashboard_registry.cpp"
        "../ui/device_status_service.cpp"
        "../ui/screen_controller.cpp"
        "../ui/scan_engine.cpp"
        "../ui/fm_dashboard.cpp"
        "../ui/fm_config.cpp"
        "../ui/home_dashboard.cpp"
        "../ui/lora_dashboard.cpp"
        "../ui/lora_packet_log.cpp"
        "../ui/lora_history.cpp"
        "../ui/lora_native_decoder.cpp"
        "../ui/lora_preamble_detector.cpp"
        "../ui/lora_diag_commands.cpp"
        "../ui/lora_gain_guard.cpp"
        "../ui/realtime_log.cpp"
        "../ui/lora_channel_control.cpp"
        "../ui/location_estimate.cpp"
        "../ui/receiver_location.cpp"
        "../ui/map_pipeline.cpp"
        "../ui/map_inventory.cpp"
        "../ui/map_service.cpp"
        "../ui/map_view.cpp"
        "../ui/map_preferences.cpp"
        "../ui/map_screen.cpp"
        "../ui/map_download_notice.cpp"
        "../ui/map_serial.cpp"
        "../ui/setup_ui.cpp"
        "../ui/navigation_service.cpp"
        "../ui/nvs_store.cpp"
        "../ui/p25_dashboard.cpp"
        "../ui/p25_config.cpp"
        "../ui/p25_decoder.cpp"
        "../ui/p25_decoder_core.cpp"
        "../ui/p25_voice.cpp"
        "../ui/acars_app.cpp"
        "../ui/acars_archive.cpp"
        "../ui/acars_archive_sd.cpp"
        "../ui/acars_core.cpp"
        "../ui/acars_dashboard.cpp"
        "../ui/acars_dashboard_layout.cpp"
        "../ui/acars_dirty_rects.cpp"
        "../ui/acars_dashboard_model.cpp"
        "../ui/acars_dashboard_ui.cpp"
        "../ui/acars_receiver.cpp"
        "../ui/acars_selftest.cpp"
        "../ui/acars_demo_data.cpp"
        "../ui/acars_interpret.cpp"
        "../ui/vdl2_burst.cpp"
        "../ui/vdl2_channel.cpp"
        "../ui/vdl2_frames.cpp"
        "../ui/vdl2_receiver.cpp"
        "../ui/vdl2_rs.cpp"
        "../ui/pocsag_dashboard.cpp"
        "../ui/pocsag_decoder_core.cpp"
        "../ui/pocsag_store.cpp"
        "../ui/radio_session.cpp"
        "../ui/radio_ui_service.cpp"
        "../ui/rf_analysis.cpp"
        "../ui/rf_lab.cpp"
        "../ui/rf_visualizer.cpp"
        "../ui/rf_visualizer_controls.cpp"
        "../ui/settings_app.cpp"
        "../ui/text_editor.cpp"
        "../ui/keyboard_input.cpp"
        "../ui/tab5_keyboard.cpp"
        "../ui/focus_nav.cpp"
        "../ui/focus_ring.cpp"
        "../ui/filter_standards.cpp"
        "../ui/band_profile.cpp"
        "../ui/home_vfo.cpp"
        "../ui/waterfall_style.cpp"
        "../ui/airband_audio_filter.cpp"
        "../ui/freq_keypad.cpp"
        "../dsp/dsp_stats.cpp"
        "../ui/time_service.cpp"
        "../ui/clock_settings.cpp"
        "../ui/ntp_sync.cpp"
        "../ui/clock_sync_status.cpp"
        "../ui/ui_capture.cpp"
        "../ui/web_console.cpp"
        "../ui/web_audio_transport.cpp"
        "../ui/dead_air_model.cpp"
        "../ui/dead_air_journal.cpp"
        "../ui/dead_air_sd_journal.cpp"
        "../ui/dead_air_dashboard.cpp"
        "../ui/dead_air_app.cpp"
        "../ui/dead_air_sweep.cpp"
        "../ui/dead_air_palette.cpp"
        "../ui/dead_air_audio.cpp"
        "../ui/dead_air_clip.cpp"
        "../ui/dead_air_format.cpp"
        "../ui/dead_air_bandplan.cpp"
        "../ui/dead_air_control.cpp"
        "../ui/dead_air_fx.cpp"
        "../ui/dead_air_stats.cpp"
        "../ui/dead_air_env.cpp"
        "../ui/dead_air_env_m5.cpp"
        "../ui/dead_air_envevents.cpp"
        "../ui/dead_air_veil26.cpp"
        "../ui/dead_air_runes.cpp"
        "../ui/dead_air_veil26_log.cpp"
        "../ui/dead_air_info.cpp"
        "../ui/dead_air_mic.cpp"
        "../ui/rf24_dashboard.cpp"
        "../ui/wifi_service.cpp"
    INCLUDE_DIRS "." "../ui" "../dsp" "${CMAKE_CURRENT_BINARY_DIR}"
    REQUIRES
        bootloader_support
        mbedtls
        m5unified
        m5gfx
        mbelib
        nvs_flash
        esp_rtl_sdr
        orcmaps
        esp_driver_usb_serial_jtag
        esp_driver_i2c
        esp_driver_gpio
        esp_driver_jpeg
        fatfs
        esp_hosted
        esp_event
        esp_netif
        lwip
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
target_compile_definitions(${COMPONENT_LIB} PRIVATE ORCSDR_ORCDIAL=1)
target_compile_definitions(${COMPONENT_LIB} PRIVATE "ORCSDR_BUILD_ID=\"${ORCSDR_BUILD_ID}\"")

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
option(ORCSDR_MAP_STARTUP_DIAG "Measure pre-app DMA reservation; test builds only" OFF)
if(ORCSDR_MAP_STARTUP_DIAG)
    target_sources(${COMPONENT_LIB} PRIVATE "../ui/map_startup_diagnostics.cpp")
    target_link_libraries(${COMPONENT_LIB} INTERFACE
        "-Wl,--wrap=esp_psram_extram_reserve_dma_pool"
        "-Wl,--undefined=__wrap_esp_psram_extram_reserve_dma_pool")
endif()
option(ORCSDR_WIFI_RX_DIAG "Instrument Wi-Fi receive callbacks; test builds only" OFF)
if(ORCSDR_WIFI_RX_DIAG)
    target_sources(${COMPONENT_LIB} PRIVATE "../ui/wifi_rx_diagnostics.cpp")
    target_link_libraries(${COMPONENT_LIB} INTERFACE
        "-Wl,--wrap=esp_netif_receive" "-Wl,--undefined=__wrap_esp_netif_receive")
endif()
# DSP build switches (tools/build-tab5-idf.ps1 always passes all of them, so a value
# cached by an earlier build can never leak into this one).
set(ORCSDR_DSP_AB 0 CACHE STRING "1 = Stage-1 old-vs-new DSP A/B harness (test builds only)")
set(ORCSDR_DSP_STAGE_TIMING 1 CACHE STRING "1 = per-stage DSP timing in RTL_DSP STATS")
option(ORCSDR_C6_FAULT_TEST "Enable authenticated C6 power interruption diagnostics; never ship" OFF)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    ORCSDR_HAS_EMBEDDED_C6_FIRMWARE=$<BOOL:${C6_FIRMWARE_BIN}>
    ORCSDR_DSP_AB=$<BOOL:${ORCSDR_DSP_AB}>
    ORCSDR_C6_FAULT_TEST=$<BOOL:${ORCSDR_C6_FAULT_TEST}>
    ORCSDR_DSP_STAGE_TIMING=$<BOOL:${ORCSDR_DSP_STAGE_TIMING}>
    ORCSDR_DSP_LAB=$<BOOL:${ORCSDR_DSP_LAB}>)
target_compile_options(${COMPONENT_LIB} PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:-include;orcsdr_m5_time.hpp>")
