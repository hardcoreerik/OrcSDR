cmake_minimum_required(VERSION 3.16)
project(orc_cardputer_zero_acars LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(ORC_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
set(ORC_TAB5_UI "${ORC_REPO_ROOT}/apps/orcsdr-tab5/ui")
set(ORC_CARDPUTER "${ORC_REPO_ROOT}/apps/orcsdr-cardputer-zero/src")
add_library(orc_zero_acars STATIC
  acars_dashboard.cpp acars_renderer.cpp
  "${ORC_TAB5_UI}/acars_core.cpp")
target_include_directories(orc_zero_acars PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}" "${ORC_TAB5_UI}" "${ORC_CARDPUTER}" "${ORC_CARDPUTER}/compact")
target_compile_options(orc_zero_acars PRIVATE -Wall -Wextra -Wpedantic)

add_executable(orc_zero_acars_preview preview.cpp)
target_link_libraries(orc_zero_acars_preview PRIVATE orc_zero_acars)

include(CTest)
if(BUILD_TESTING)
  add_executable(orc_zero_acars_test tests/acars_dashboard_test.cpp "${ORC_TAB5_UI}/acars_selftest.cpp")
  target_link_libraries(orc_zero_acars_test PRIVATE orc_zero_acars)
  target_compile_options(orc_zero_acars_test PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME staged_zero_acars_regressions COMMAND orc_zero_acars_test)
  add_test(NAME staged_zero_acars_preview COMMAND orc_zero_acars_preview
    --demo --screenshot "${CMAKE_CURRENT_BINARY_DIR}/acars-demo.ppm")
  add_test(NAME staged_zero_acars_empty COMMAND orc_zero_acars_preview
    --demo --empty --screenshot "${CMAKE_CURRENT_BINARY_DIR}/acars-empty.ppm")
  add_test(NAME staged_zero_acars_detail COMMAND orc_zero_acars_preview
    --demo --detail --screenshot "${CMAKE_CURRENT_BINARY_DIR}/acars-detail.ppm")
endif()
