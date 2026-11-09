cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")
set(ENV{EXTRA_CPPFLAGS}
    "$ENV{EXTRA_CPPFLAGS} -Wno-error=stringop-truncation -Wno-error=maybe-uninitialized -DRTL_USE_LEGACY_USB=0")
# Data-pack tags must not become the application firmware version.  Keep the
# catalog compatibility gate tied to the most recent semantic firmware tag.
execute_process(
    COMMAND git describe --tags --match "v[0-9]*"
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../.."
    OUTPUT_VARIABLE ORCSDR_GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
string(REGEX REPLACE "^(v[0-9]+\\.[0-9]+\\.[0-9]+(-(alpha|beta|rc)\\.?[0-9]+)?).*$" "\\1"
       PROJECT_VER "${ORCSDR_GIT_VERSION}")
if (NOT PROJECT_VER MATCHES "^v[0-9]+\\.[0-9]+\\.[0-9]+")
    set(PROJECT_VER "v0.0.0-alpha.0")
endif()
# Exact build identity for Settings > System > BUILD (e.g. "nightly a914882 2026-10-07").
# CI passes -D ORCSDR_BUILD_ID=...; empty keeps the old compile-date display. PROJECT_VER
# above stays the base release tag because the catalog compatibility gate parses it.
set(ORCSDR_BUILD_ID "" CACHE STRING "Build identity shown on Settings > System (<=39 chars)")
if (NOT ORCSDR_BUILD_ID MATCHES "^[A-Za-z0-9 ._:+/-]*$")
    message(FATAL_ERROR "ORCSDR_BUILD_ID has unsupported characters: ${ORCSDR_BUILD_ID}")
endif()
string(LENGTH "${ORCSDR_BUILD_ID}" ORCSDR_BUILD_ID_LEN)
if (ORCSDR_BUILD_ID_LEN GREATER 39)
    message(FATAL_ERROR "ORCSDR_BUILD_ID is longer than 39 characters: ${ORCSDR_BUILD_ID}")
endif()
# Materialize the reviewed SDK patch per build before IDF resolves components.
if(NOT PYTHON)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(PYTHON "${Python3_EXECUTABLE}")
endif()
execute_process(
    COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/tools/prepare_usb_recovery.py"
            --idf "$ENV{IDF_PATH}" --build "${CMAKE_BINARY_DIR}"
    RESULT_VARIABLE usb_recovery_result
    OUTPUT_VARIABLE usb_recovery_component
    ERROR_VARIABLE usb_recovery_error
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT usb_recovery_result EQUAL 0)
    message(FATAL_ERROR "${usb_recovery_error}")
endif()
list(APPEND EXTRA_COMPONENT_DIRS "${usb_recovery_component}")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(orcsdr_tab5)

execute_process(COMMAND ${PYTHON} "${CMAKE_CURRENT_LIST_DIR}/tools/check-orcmaps-pin.py"
    "${CMAKE_CURRENT_LIST_DIR}" RESULT_VARIABLE orcmaps_pin_result)
if(NOT orcmaps_pin_result EQUAL 0)
    message(FATAL_ERROR "OrcMaps requested/locked/fetched revision mismatch; resolve this component before building")
endif()

# Keep the ROM/miniz collision gate on every build path, including idf.py.
idf_component_get_property(orcmaps_dir orcmaps COMPONENT_DIR)
add_custom_target(orcsdr_orcmaps_symbols ALL
    COMMAND ${PYTHON} "${orcmaps_dir}/tools/check_miniz_symbols.py"
            "${CMAKE_BINARY_DIR}" --nm "${CMAKE_NM}"
    DEPENDS "${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.elf"
    VERBATIM)

add_custom_target(orcsdr_ft8_scratch_placement ALL
    COMMAND ${PYTHON} "${CMAKE_CURRENT_LIST_DIR}/../../tools/check_tab5_ft8_scratch.py"
            "${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.elf" --nm "${CMAKE_NM}"
    DEPENDS "${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.elf"
    VERBATIM)

# Optional, verified data artifact. An empty value never writes a world map.
set(ORCSDR_WORLD_MAP "" CACHE FILEPATH "Natural Earth z0-z4 PMTiles archive to provision")
if(ORCSDR_WORLD_MAP)
    file(SHA256 "${ORCSDR_WORLD_MAP}" world_sha)
    if(NOT world_sha STREQUAL "9aea08772bacf1f024d1da90cc52aa8fcf0b0e37405415dc7c91e75e56596f0f")
        message(FATAL_ERROR "World map does not match the reviewed Natural Earth z0-z4 artifact")
    endif()
    esptool_py_flash_to_partition(flash "orcmaps_world" "${ORCSDR_WORLD_MAP}")
endif()
