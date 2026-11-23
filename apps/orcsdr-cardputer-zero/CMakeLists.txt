cmake_minimum_required(VERSION 3.16)
project(orcsdr_cardputer_zero LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compile the EXISTING OrcSDR decoder implementations directly. Do not
# vendor, copy, or modify them for Linux. ESP-IDF firmware remains untouched.
get_filename_component(ORC_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(ORC_NATIVE_DECODER_DIR "${ORC_REPO_ROOT}/apps/orcsdr-tab5/ui")
add_library(orc_native_decoders STATIC
  "${ORC_NATIVE_DECODER_DIR}/acars_core.cpp"
  "${ORC_NATIVE_DECODER_DIR}/acars_selftest.cpp"
  "${ORC_NATIVE_DECODER_DIR}/acars_dashboard_model.cpp"
  "${ORC_NATIVE_DECODER_DIR}/acars_demo_data.cpp"
  "${ORC_NATIVE_DECODER_DIR}/acars_archive.cpp"
  "${ORC_NATIVE_DECODER_DIR}/adsb_decoder.cpp"
  "${ORC_NATIVE_DECODER_DIR}/shortwave_ssb.cpp"
  "${ORC_NATIVE_DECODER_DIR}/ft8_audio_tap.cpp"
  "${ORC_NATIVE_DECODER_DIR}/ft8_native_backend.cpp"
  "${ORC_NATIVE_DECODER_DIR}/ft8_spectral_fft.cpp"
  "${ORC_NATIVE_DECODER_DIR}/ft8_mode.cpp"
  "${ORC_NATIVE_DECODER_DIR}/ft8_sync.cpp"
  "${ORC_NATIVE_DECODER_DIR}/ft8_demod.cpp"
  "${ORC_NATIVE_DECODER_DIR}/ft8_pipeline.cpp"
  "${ORC_NATIVE_DECODER_DIR}/ft8_snr.cpp"
  "${ORC_NATIVE_DECODER_DIR}/ft8_message.cpp"
  "${ORC_NATIVE_DECODER_DIR}/ft8_codec.cpp"
  "${ORC_NATIVE_DECODER_DIR}/ft8_ldpc.cpp"
  "${ORC_NATIVE_DECODER_DIR}/ft8_ldpc_decode.cpp"
  "${ORC_NATIVE_DECODER_DIR}/ft8_model.cpp"
  "${ORC_NATIVE_DECODER_DIR}/ft8_world_data.cpp"
  "${ORC_NATIVE_DECODER_DIR}/ft8_tuning.cpp"
  "${ORC_NATIVE_DECODER_DIR}/p25_decoder_core.cpp"
  "${ORC_NATIVE_DECODER_DIR}/p25_voice.cpp"
  "${ORC_REPO_ROOT}/components/mbelib/mbelib.c"
  "${ORC_REPO_ROOT}/components/mbelib/imbe7200x4400.c"
  "${ORC_REPO_ROOT}/components/mbelib/ecc.c"
  "${ORC_NATIVE_DECODER_DIR}/dead_air_model.cpp"
  "${ORC_NATIVE_DECODER_DIR}/dead_air_journal.cpp"
  "${ORC_NATIVE_DECODER_DIR}/dead_air_runes.cpp"
  "${ORC_NATIVE_DECODER_DIR}/pocsag_decoder_core.cpp"
  "${ORC_NATIVE_DECODER_DIR}/sstv_receiver.cpp"
  "${ORC_NATIVE_DECODER_DIR}/sstv_modes.cpp"
  "${ORC_NATIVE_DECODER_DIR}/sstv_auto_detect.cpp"
)
target_include_directories(orc_native_decoders PUBLIC "${ORC_NATIVE_DECODER_DIR}" "${ORC_REPO_ROOT}/components/mbelib")
target_compile_options(orc_native_decoders PRIVATE -Wall -Wextra -Wpedantic)

add_library(orc_linux_iq INTERFACE)
target_include_directories(orc_linux_iq INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")
find_package(Threads REQUIRED)
add_library(orc_linux_runtime STATIC src/rtl_receiver.cpp src/live_capture.cpp src/web_server.cpp)
target_link_libraries(orc_linux_runtime PUBLIC orc_linux_iq orc_native_decoders Threads::Threads)
target_include_directories(orc_linux_runtime PUBLIC "${ORC_REPO_ROOT}/contracts/radio_service/v1/include")
target_compile_options(orc_linux_runtime PRIVATE -Wall -Wextra -Wpedantic)
add_executable(orcsdr_cardputer_zero src/main.cpp)
target_link_libraries(orcsdr_cardputer_zero PRIVATE orc_linux_runtime)
target_compile_options(orcsdr_cardputer_zero PRIVATE -Wall -Wextra -Wpedantic)


# Original portable DSP and 320x170 compact native framebuffer renderer.
# These targets do not depend on SDL, ALSA or a physical SDR.
set(ORC_ZERO_RADIO_SOURCE "${ORC_REPO_ROOT}/core/portable/cardputer_radio")
add_library(orc_zero_fm_dsp STATIC
  "${ORC_ZERO_RADIO_SOURCE}/fm_dsp.c"
  "${ORC_ZERO_RADIO_SOURCE}/scan_dsp.c")
target_include_directories(orc_zero_fm_dsp PUBLIC "${ORC_ZERO_RADIO_SOURCE}")
target_link_libraries(orc_zero_fm_dsp PUBLIC m)
add_library(orc_zero_waterfall_style STATIC "${ORC_NATIVE_DECODER_DIR}/waterfall_style.cpp")
target_include_directories(orc_zero_waterfall_style PUBLIC "${ORC_NATIVE_DECODER_DIR}")
add_library(orc_zero_compact INTERFACE)
target_include_directories(orc_zero_compact INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src/compact")
target_link_libraries(orc_zero_compact INTERFACE orc_zero_waterfall_style)
add_executable(orc_zero_screenshot src/compact/screenshot_main.cpp)
target_link_libraries(orc_zero_screenshot PRIVATE orc_zero_compact)
target_compile_options(orc_zero_screenshot PRIVATE -Wall -Wextra -Wpedantic)

option(ORC_ENABLE_RTLSDR "Enable optional system librtlsdr receiver" ON)
if(ORC_ENABLE_RTLSDR)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(RTLSDR QUIET IMPORTED_TARGET librtlsdr)
  endif()
  if(RTLSDR_FOUND)
    target_link_libraries(orc_linux_runtime PRIVATE PkgConfig::RTLSDR)
    target_compile_definitions(orc_linux_runtime PRIVATE ORC_HAVE_RTLSDR=1)
    message(STATUS "OrcSDR: system librtlsdr enabled")
  else()
    message(STATUS "OrcSDR: system librtlsdr unavailable; offline/demo modes only")
  endif()
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(orc_zero_fm_test tests/fm_audio_dsp_test.cpp)
  target_link_libraries(orc_zero_fm_test PRIVATE orc_zero_fm_dsp)
  target_compile_options(orc_zero_fm_test PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME original_wfm_audio_dsp COMMAND orc_zero_fm_test)
  add_executable(orc_compact_control_test tests/compact_control_test.cpp)
  target_link_libraries(orc_compact_control_test PRIVATE orc_zero_compact)
  add_test(NAME compact_control_state COMMAND orc_compact_control_test)
  add_test(NAME compact_render_snapshot COMMAND orc_zero_screenshot main "${CMAKE_CURRENT_BINARY_DIR}/orc-zero-main.ppm")
  add_executable(orc_linux_iq_test tests/metrics_test.cpp)
  target_link_libraries(orc_linux_iq_test PRIVATE orc_linux_iq)
  target_compile_options(orc_linux_iq_test PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME iq_metrics COMMAND orc_linux_iq_test)
  add_test(NAME simulated_receiver COMMAND orcsdr_cardputer_zero --demo)
  add_test(NAME native_acars_cli_demo COMMAND orcsdr_cardputer_zero --acars-demo)
  add_test(NAME make_acars_fixture COMMAND orcsdr_cardputer_zero
    --make-acars-fixture "${CMAKE_CURRENT_BINARY_DIR}/acars-fixture.cu8")
  set_tests_properties(make_acars_fixture PROPERTIES FIXTURES_SETUP acars_file)
  add_test(NAME decode_acars_fixture COMMAND orcsdr_cardputer_zero
    --decode-acars "${CMAKE_CURRENT_BINARY_DIR}/acars-fixture.cu8" --freq 131490000)
  set_tests_properties(decode_acars_fixture PROPERTIES FIXTURES_REQUIRED acars_file)

  add_executable(orc_native_decoder_test tests/native_decoder_test.cpp)
  target_link_libraries(orc_native_decoder_test PRIVATE orc_native_decoders)
  target_compile_options(orc_native_decoder_test PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME native_acars_adsb_regression COMMAND orc_native_decoder_test)

  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME linux_web_assets_loopback
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/web_server_test.py"
              "$<TARGET_FILE:orcsdr_cardputer_zero>")
  endif()

  add_executable(orc_linux_v1_frame_test tests/v1_frame_adapter_test.cpp)
  target_link_libraries(orc_linux_v1_frame_test PRIVATE orc_linux_runtime)
  target_compile_options(orc_linux_v1_frame_test PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME linux_radio_service_v1_frame COMMAND orc_linux_v1_frame_test)

  add_executable(orc_live_capture_test tests/live_capture_test.cpp)
  target_link_libraries(orc_live_capture_test PRIVATE orc_linux_runtime)
  target_compile_options(orc_live_capture_test PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME live_native_acars_pipeline COMMAND orc_live_capture_test)

  add_executable(orc_iq_queue_test tests/iq_queue_test.cpp)
  target_link_libraries(orc_iq_queue_test PRIVATE orc_linux_iq)
  target_compile_options(orc_iq_queue_test PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME bounded_iq_queue COMMAND orc_iq_queue_test)
endif()

# The CLI remains buildable without graphical/audio development headers.
option(ORC_ENABLE_GUI "Build native Cardputer Zero compact graphic radio" ON)
option(ORC_ENABLE_ALSA "Enable ALSA PCM output for the compact radio" ON)
option(ORC_ENABLE_SDL "Enable SDL2 desktop preview for the compact radio" ON)
if(ORC_ENABLE_GUI)
  add_library(orc_zero_audio STATIC src/linux_audio.cpp)
  target_include_directories(orc_zero_audio PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(orc_zero_audio PRIVATE -Wall -Wextra -Wpedantic)
  if(ORC_ENABLE_ALSA AND PkgConfig_FOUND)
    pkg_check_modules(ORC_ALSA QUIET IMPORTED_TARGET alsa)
    if(ORC_ALSA_FOUND)
      target_link_libraries(orc_zero_audio PRIVATE PkgConfig::ORC_ALSA)
      target_compile_definitions(orc_zero_audio PRIVATE ORC_HAVE_ALSA=1)
      message(STATUS "OrcSDR: ALSA PCM enabled")
    else()
      message(STATUS "OrcSDR: ALSA unavailable; GUI remains functional without audio")
    endif()
  endif()
  add_library(orc_zero_fm_session STATIC src/fm_session.cpp)
  target_link_libraries(orc_zero_fm_session PUBLIC orc_linux_runtime orc_zero_fm_dsp orc_zero_compact orc_zero_audio Threads::Threads)
  target_compile_options(orc_zero_fm_session PRIVATE -Wall -Wextra -Wpedantic)
  add_library(orc_zero_settings STATIC src/settings.cpp)
  target_include_directories(orc_zero_settings PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(orc_zero_settings PRIVATE -Wall -Wextra -Wpedantic)
  add_executable(orcsdr_cardputer_zero_gui src/gui_main.cpp)
  target_link_libraries(orcsdr_cardputer_zero_gui PRIVATE orc_zero_fm_session orc_zero_settings)
  foreach(dashboard adsb acars airband)
    target_include_directories(orcsdr_cardputer_zero_gui PRIVATE staged-dashboards/${dashboard})
  endforeach()
  target_sources(orcsdr_cardputer_zero_gui PRIVATE
    staged-dashboards/adsb/adsb_module.cpp staged-dashboards/adsb/adsb_renderer.cpp
    staged-dashboards/acars/acars_dashboard.cpp staged-dashboards/acars/acars_renderer.cpp
    staged-dashboards/acars/acars_archive_worker.cpp
    staged-dashboards/airband/airband.cpp staged-dashboards/airband/airband_render.cpp)

  target_compile_options(orcsdr_cardputer_zero_gui PRIVATE -Wall -Wextra -Wpedantic)
  if(ORC_ENABLE_SDL AND PkgConfig_FOUND)
    pkg_check_modules(ORC_SDL QUIET IMPORTED_TARGET sdl2)
    if(ORC_SDL_FOUND)
      target_link_libraries(orcsdr_cardputer_zero_gui PRIVATE PkgConfig::ORC_SDL)
      target_compile_definitions(orcsdr_cardputer_zero_gui PRIVATE ORC_HAVE_SDL=1)
      message(STATUS "OrcSDR: SDL2 simulator enabled")
    else()
      message(STATUS "OrcSDR: SDL2 unavailable; fbdev/screenshots remain available")
    endif()
  endif()
  install(TARGETS orcsdr_cardputer_zero_gui RUNTIME DESTINATION bin)
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/tools/orcsdr-cardputer.desktop"
    DESTINATION share/APPLaunch/applications)
  install(FILES "${ORC_REPO_ROOT}/apps/orcsdr-tab5/main/orc_badge_104.png"
    DESTINATION share/APPLaunch/share/images RENAME orcsdr-cardputer.png)
  if(BUILD_TESTING)
    add_executable(orc_zero_settings_test tests/settings_test.cpp)
    target_link_libraries(orc_zero_settings_test PRIVATE orc_zero_settings)
    add_test(NAME zero_settings_roundtrip COMMAND orc_zero_settings_test)
    add_executable(orc_zero_audio_test tests/linux_audio_test.cpp)
    target_link_libraries(orc_zero_audio_test PRIVATE orc_zero_audio)
    add_test(NAME zero_audio_null_mute COMMAND orc_zero_audio_test)
  endif()
endif()

# Device packaging remains opt-in: never install a system service by default.
set(CPACK_PACKAGE_NAME "orcsdr-cardputer-zero")
set(CPACK_PACKAGE_VENDOR "Oregon Radio Company")
set(CPACK_PACKAGE_VERSION "0.1.0")
set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_PACKAGE_CONTACT "Oregon Radio Company")
include(CPack)

if(ORC_ENABLE_GUI)
  target_link_libraries(orc_zero_settings PUBLIC orc_zero_compact)
  if(BUILD_TESTING)
    add_executable(orc_zero_home_ui_test tests/home_ui_test.cpp)
    target_link_libraries(orc_zero_home_ui_test PRIVATE orc_zero_settings orc_zero_compact)
    add_test(NAME zero_home_layout_palette COMMAND orc_zero_home_ui_test)
    foreach(test_target orc_zero_home_ui_test orc_zero_fm_test orc_compact_control_test orc_zero_settings_test orc_zero_audio_test)
      if(MSVC)
        target_compile_options(${test_target} PRIVATE /UNDEBUG)
      else()
        target_compile_options(${test_target} PRIVATE -UNDEBUG)
      endif()
    endforeach()
  endif()
endif()

if(BUILD_TESTING)
  add_executable(orc_zero_ft8_band_test tests/ft8_band_test.cpp)
  target_include_directories(orc_zero_ft8_band_test PRIVATE src "${ORC_NATIVE_DECODER_DIR}")
  target_link_libraries(orc_zero_ft8_band_test PRIVATE orc_native_decoders)
  if(MSVC)
    target_compile_options(orc_zero_ft8_band_test PRIVATE /UNDEBUG)
  else()
    target_compile_options(orc_zero_ft8_band_test PRIVATE -UNDEBUG)
  endif()
  add_test(NAME zero_ft8_band_presets COMMAND orc_zero_ft8_band_test)
endif()

if(BUILD_TESTING)
  add_executable(orc_zero_p25_test tests/p25_test.cpp)
  target_include_directories(orc_zero_p25_test PRIVATE src src/compact)
  target_link_libraries(orc_zero_p25_test PRIVATE orc_native_decoders orc_zero_compact)
  if(MSVC)
    target_compile_options(orc_zero_p25_test PRIVATE /UNDEBUG)
  else()
    target_compile_options(orc_zero_p25_test PRIVATE -UNDEBUG)
  endif()
  add_test(NAME zero_p25_decoder_ui COMMAND orc_zero_p25_test)
  add_executable(orc_zero_p25_profile_scan_test tests/p25_profile_scan_test.cpp)
  target_include_directories(orc_zero_p25_profile_scan_test PRIVATE src)
  target_compile_options(orc_zero_p25_profile_scan_test PRIVATE -UNDEBUG)
  add_test(NAME zero_p25_profile_scan COMMAND orc_zero_p25_profile_scan_test)
endif()

# The LoRa core stays shared; only platform services are supplied by Linux.
find_path(ORC_MBEDTLS_INCLUDE_DIR mbedtls/aes.h REQUIRED)
find_library(ORC_MBEDCRYPTO_LIBRARY mbedcrypto REQUIRED)
add_library(orc_zero_lora STATIC
  "${ORC_NATIVE_DECODER_DIR}/lora_native_decoder.cpp"
  "${ORC_NATIVE_DECODER_DIR}/lora_preamble_detector.cpp"
  src/lora_linux/esp_fft_ansi.c)
target_include_directories(orc_zero_lora PUBLIC src "${ORC_NATIVE_DECODER_DIR}" "${ORC_MBEDTLS_INCLUDE_DIR}")
target_compile_definitions(orc_zero_lora PRIVATE ORC_LORA_LINUX=1)
target_link_libraries(orc_zero_lora PUBLIC "${ORC_MBEDCRYPTO_LIBRARY}" Threads::Threads)
if(BUILD_TESTING)
  add_executable(orc_zero_lora_test tests/lora_test.cpp)
  target_link_libraries(orc_zero_lora_test PRIVATE orc_zero_lora orc_linux_runtime)
  if(MSVC)
    target_compile_options(orc_zero_lora_test PRIVATE /UNDEBUG)
  else()
    target_compile_options(orc_zero_lora_test PRIVATE -UNDEBUG)
  endif()
  add_test(NAME zero_lora_native_linux COMMAND orc_zero_lora_test)
endif()

# Meshtastic application layer: the 16-byte MeshPacket header, AES-128-CTR with
# the channel key, and the Data protobuf (portnum + payload). The LoRa PHY -
# symbols, whitening, CRC - stays where it is, in the shared native decoder.
add_library(orc_zero_meshtastic STATIC src/mesh/meshtastic_packet.cpp)
target_include_directories(orc_zero_meshtastic PUBLIC src "${ORC_MBEDTLS_INCLUDE_DIR}")
target_link_libraries(orc_zero_meshtastic PUBLIC "${ORC_MBEDCRYPTO_LIBRARY}" Threads::Threads)

if(BUILD_TESTING)
  # Station index reader. The index is generated from the FCC snapshot and is not
  # committed, so the test skips when it has not been built.
  add_executable(orc_zero_station_directory_test tests/station_directory_test.cpp)
  target_include_directories(orc_zero_station_directory_test PRIVATE src)
  target_compile_options(orc_zero_station_directory_test PRIVATE -UNDEBUG)
  add_test(NAME zero_station_directory
           COMMAND orc_zero_station_directory_test "${CMAKE_CURRENT_SOURCE_DIR}/fm_am_stations.idx")
endif()
if(BUILD_TESTING)
  # Protocol-neutral LoRa model: the contract every adapter fills and every
  # screen reads.
  add_executable(orc_zero_lora_model_test tests/lora_model_test.cpp)
  target_include_directories(orc_zero_lora_model_test PRIVATE src)
  target_compile_options(orc_zero_lora_model_test PRIVATE -UNDEBUG)
  add_test(NAME zero_lora_model COMMAND orc_zero_lora_model_test)
endif()
if(BUILD_TESTING)
  add_executable(orc_zero_meshtastic_test tests/meshtastic_decode_test.cpp)
  target_link_libraries(orc_zero_meshtastic_test PRIVATE orc_zero_meshtastic)
  target_compile_options(orc_zero_meshtastic_test PRIVATE -UNDEBUG)
  add_test(NAME zero_meshtastic_decode COMMAND orc_zero_meshtastic_test)
endif()

install(FILES src/lora_linux/LICENSE src/lora_linux/PROVENANCE.md DESTINATION share/doc/orcsdr-cardputer-zero/esp-dsp)

install(PROGRAMS tools/orcsdr-clock DESTINATION libexec)

if(BUILD_TESTING)
  add_executable(orc_zero_ft8_clock_slots_test tests/ft8_slots_test.cpp)
  target_include_directories(orc_zero_ft8_clock_slots_test PRIVATE src)
  if(MSVC)
    target_compile_options(orc_zero_ft8_clock_slots_test PRIVATE /UNDEBUG)
  else()
    target_compile_options(orc_zero_ft8_clock_slots_test PRIVATE -UNDEBUG)
  endif()
  add_test(NAME zero_ft8_clock_discontinuity COMMAND orc_zero_ft8_clock_slots_test)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME zero_clock_helper_validation COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_clock_helper.py")
  endif()
endif()

if(BUILD_TESTING)
  add_executable(orc_zero_ft8_visuals_test tests/ft8_visuals_test.cpp)
  target_link_libraries(orc_zero_ft8_visuals_test PRIVATE orc_native_decoders orc_zero_compact)
  target_include_directories(orc_zero_ft8_visuals_test PRIVATE src)
  target_compile_options(orc_zero_ft8_visuals_test PRIVATE -UNDEBUG)
  add_test(NAME zero_ft8_waterfall_map COMMAND orc_zero_ft8_visuals_test)
endif()

if(TARGET orcsdr_cardputer_zero_gui)
  target_link_libraries(orcsdr_cardputer_zero_gui PRIVATE orc_zero_lora)
endif()
if(BUILD_TESTING)
  add_executable(orc_zero_lora_ui_test tests/lora_ui_test.cpp)
  target_link_libraries(orc_zero_lora_ui_test PRIVATE orc_zero_lora orc_native_decoders orc_zero_compact)
  target_compile_options(orc_zero_lora_ui_test PRIVATE -UNDEBUG)
  add_test(NAME zero_lora_compact_ui COMMAND orc_zero_lora_ui_test)
  add_executable(orc_zero_lora_history_test tests/lora_history_test.cpp)
  target_include_directories(orc_zero_lora_history_test PRIVATE src)
  target_link_libraries(orc_zero_lora_history_test PRIVATE orc_native_decoders)
  target_compile_options(orc_zero_lora_history_test PRIVATE -UNDEBUG)
  add_test(NAME zero_lora_packet_history COMMAND orc_zero_lora_history_test)
endif()

if(BUILD_TESTING)
  add_executable(orc_zero_satellite_test tests/satellite_test.cpp)
  target_include_directories(orc_zero_satellite_test PRIVATE src)
  target_link_libraries(orc_zero_satellite_test PRIVATE orc_native_decoders orc_zero_compact orc_zero_fm_dsp)
  target_compile_options(orc_zero_satellite_test PRIVATE -UNDEBUG)
  add_test(NAME zero_satellite_compact COMMAND orc_zero_satellite_test)
endif()

if(BUILD_TESTING)
  add_executable(orc_zero_bias_tee_test tests/bias_tee_test.cpp)
  target_include_directories(orc_zero_bias_tee_test PRIVATE src)
  target_compile_options(orc_zero_bias_tee_test PRIVATE -UNDEBUG)
  add_test(NAME zero_bias_tee_control COMMAND orc_zero_bias_tee_test)
endif()

if(BUILD_TESTING)
  add_executable(orc_zero_acars_tuning_test tests/acars_tuning_test.cpp staged-dashboards/acars/acars_dashboard.cpp staged-dashboards/acars/acars_renderer.cpp)
  target_include_directories(orc_zero_acars_tuning_test PRIVATE src staged-dashboards/acars)
  target_link_libraries(orc_zero_acars_tuning_test PRIVATE orc_native_decoders orc_zero_compact)
  target_compile_options(orc_zero_acars_tuning_test PRIVATE -UNDEBUG)
  add_test(NAME zero_acars_channel_tuning COMMAND orc_zero_acars_tuning_test)
endif()

if(BUILD_TESTING)
  add_executable(orc_zero_p25_follow_test tests/p25_follow_test.cpp)
  target_include_directories(orc_zero_p25_follow_test PRIVATE src "${ORC_NATIVE_DECODER_DIR}")
  target_compile_options(orc_zero_p25_follow_test PRIVATE -UNDEBUG)
  add_test(NAME zero_p25_phase1_follow COMMAND orc_zero_p25_follow_test)
endif()

if(BUILD_TESTING)
  add_executable(orc_zero_dead_air_storage_test tests/dead_air_storage_test.cpp)
  target_include_directories(orc_zero_dead_air_storage_test PRIVATE src)
  target_link_libraries(orc_zero_dead_air_storage_test PRIVATE orc_native_decoders orc_zero_compact Threads::Threads)
  target_compile_options(orc_zero_dead_air_storage_test PRIVATE -UNDEBUG)
  add_test(NAME zero_dead_air_journal COMMAND orc_zero_dead_air_storage_test)
endif()

if(BUILD_TESTING)
  foreach(case ft4_slots ft4_receiver)
    add_executable(orc_zero_${case}_test tests/${case}_test.cpp)
    target_include_directories(orc_zero_${case}_test PRIVATE src)
    target_link_libraries(orc_zero_${case}_test PRIVATE orc_native_decoders)
    target_compile_options(orc_zero_${case}_test PRIVATE -UNDEBUG)
    add_test(NAME zero_${case} COMMAND orc_zero_${case}_test)
  endforeach()
endif()

if(BUILD_TESTING)
  add_executable(orc_zero_branding_test tests/dashboard_branding_test.cpp
    staged-dashboards/acars/acars_renderer.cpp staged-dashboards/adsb/adsb_renderer.cpp
    staged-dashboards/airband/airband_render.cpp staged-dashboards/airband/airband.cpp)
  target_include_directories(orc_zero_branding_test PRIVATE src staged-dashboards/acars staged-dashboards/adsb staged-dashboards/airband)
  target_link_libraries(orc_zero_branding_test PRIVATE orc_native_decoders orc_zero_compact)
  target_compile_options(orc_zero_branding_test PRIVATE -UNDEBUG)
  add_test(NAME zero_dashboard_branding COMMAND orc_zero_branding_test)
endif()
