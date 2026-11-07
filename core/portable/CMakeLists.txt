cmake_minimum_required(VERSION 3.22)
project(OrcSDRPortableCore VERSION 0.1.0 LANGUAGES CXX)

# Standalone host entrypoint. Never add_subdirectory() to the ESP-IDF application:
# the firmware's source lists and production paths are deliberately untouched.
include(CTest)
get_filename_component(ORCSDR_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(ORCSDR_CANONICAL_UI "${ORCSDR_REPO_ROOT}/apps/orcsdr-tab5/ui")

function(orcsdr_add_portable_decoder target source)
    add_library(${target} STATIC "${ORCSDR_CANONICAL_UI}/${source}")
    target_compile_features(${target} PUBLIC cxx_std_17)
    target_include_directories(${target} PUBLIC "${ORCSDR_CANONICAL_UI}")
    set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# Exactly the production source files, not vendored copies or ported algorithms.
orcsdr_add_portable_decoder(orcsdr_core_acars acars_core.cpp)
orcsdr_add_portable_decoder(orcsdr_core_adsb adsb_decoder.cpp)

if(BUILD_TESTING)
    add_executable(portable_acars_smoke "${ORCSDR_REPO_ROOT}/tests/acars_core_smoke.cpp")
    target_link_libraries(portable_acars_smoke PRIVATE orcsdr_core_acars)
    add_test(NAME portable_acars_smoke COMMAND portable_acars_smoke)

    add_executable(portable_adsb_selfcheck tests/adsb_selfcheck.cpp)
    target_link_libraries(portable_adsb_selfcheck PRIVATE orcsdr_core_adsb)
    add_test(NAME portable_adsb_selfcheck COMMAND portable_adsb_selfcheck)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(portable_acars_smoke PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(portable_adsb_selfcheck PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()
