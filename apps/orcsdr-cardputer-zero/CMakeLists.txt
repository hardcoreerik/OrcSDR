cmake_minimum_required(VERSION 3.16)
project(orcsdr_cardputer_zero LANGUAGES CXX)

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
  "${ORC_NATIVE_DECODER_DIR}/adsb_decoder.cpp"
)
target_include_directories(orc_native_decoders PUBLIC "${ORC_NATIVE_DECODER_DIR}")
target_compile_options(orc_native_decoders PRIVATE -Wall -Wextra -Wpedantic)

add_library(orc_linux_iq INTERFACE)
target_include_directories(orc_linux_iq INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_executable(orcsdr_cardputer_zero src/main.cpp src/rtl_receiver.cpp)
target_link_libraries(orcsdr_cardputer_zero PRIVATE orc_linux_iq orc_native_decoders)
target_compile_options(orcsdr_cardputer_zero PRIVATE -Wall -Wextra -Wpedantic)

option(ORC_ENABLE_RTLSDR "Enable optional system librtlsdr receiver" ON)
if(ORC_ENABLE_RTLSDR)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(RTLSDR QUIET IMPORTED_TARGET librtlsdr)
  endif()
  if(RTLSDR_FOUND)
    target_link_libraries(orcsdr_cardputer_zero PRIVATE PkgConfig::RTLSDR)
    target_compile_definitions(orcsdr_cardputer_zero PRIVATE ORC_HAVE_RTLSDR=1)
    message(STATUS "OrcSDR: system librtlsdr enabled")
  else()
    message(STATUS "OrcSDR: system librtlsdr unavailable; offline/demo modes only")
  endif()
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(orc_linux_iq_test tests/metrics_test.cpp)
  target_link_libraries(orc_linux_iq_test PRIVATE orc_linux_iq)
  target_compile_options(orc_linux_iq_test PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME iq_metrics COMMAND orc_linux_iq_test)
  add_test(NAME simulated_receiver COMMAND orcsdr_cardputer_zero --demo)

  add_executable(orc_native_decoder_test tests/native_decoder_test.cpp)
  target_link_libraries(orc_native_decoder_test PRIVATE orc_native_decoders)
  target_compile_options(orc_native_decoder_test PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME native_acars_adsb_regression COMMAND orc_native_decoder_test)

  add_executable(orc_iq_queue_test tests/iq_queue_test.cpp)
  target_link_libraries(orc_iq_queue_test PRIVATE orc_linux_iq)
  target_compile_options(orc_iq_queue_test PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME bounded_iq_queue COMMAND orc_iq_queue_test)
endif()
