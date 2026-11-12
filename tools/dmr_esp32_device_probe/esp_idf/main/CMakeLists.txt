set(PROBE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(DMR_ROOT "${PROBE_ROOT}/../../components/orcsdr_dmr")
# Compile the real first-party D0/D1 files in isolation, not forked/copy-pasted.
# This standalone program does not link esp_rtl_sdr, USB, microSD, M5GFX or P25.
idf_component_register(
  SRCS
    "probe_main.cpp"
    "${PROBE_ROOT}/src/pattern.cpp"
    "${DMR_ROOT}/src/d0.cpp"
    "${DMR_ROOT}/d1_frontend/src/frontend.cpp"
    "${DMR_ROOT}/d1_protocol/src/d1.cpp"
    "${DMR_ROOT}/d1_integration/src/d1_bridge.cpp"
  INCLUDE_DIRS
    "${PROBE_ROOT}/include"
    "${DMR_ROOT}/include"
    "${DMR_ROOT}/d1_frontend/include"
    "${DMR_ROOT}/d1_protocol/include"
    "${DMR_ROOT}/d1_integration/include"
  REQUIRES esp_timer esp_psram heap freertos
)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wpedantic -Werror)
