cmake_minimum_required(VERSION 3.16)
project(orc_zero_adsb_dashboard LANGUAGES CXX)
include(CTest)
get_filename_component(ORC_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
add_library(orc_zero_adsb STATIC
  adsb_module.cpp
  adsb_renderer.cpp
  "${ORC_REPO_ROOT}/apps/orcsdr-tab5/ui/adsb_decoder.cpp")
target_compile_features(orc_zero_adsb PUBLIC cxx_std_17)
target_include_directories(orc_zero_adsb PUBLIC
  "${CMAKE_CURRENT_LIST_DIR}"
  "${ORC_REPO_ROOT}/apps/orcsdr-tab5/ui"
  "${ORC_REPO_ROOT}/apps/orcsdr-cardputer-zero/src")
if(MSVC)
  target_compile_options(orc_zero_adsb PRIVATE /W4)
else()
  target_compile_options(orc_zero_adsb PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_executable(orc_zero_adsb_preview preview.cpp)
target_link_libraries(orc_zero_adsb_preview PRIVATE orc_zero_adsb)
if(BUILD_TESTING)
  add_executable(orc_zero_adsb_tests tests/adsb_tests.cpp)
  target_link_libraries(orc_zero_adsb_tests PRIVATE orc_zero_adsb)
  add_test(NAME adsb_focused COMMAND orc_zero_adsb_tests)
endif()
