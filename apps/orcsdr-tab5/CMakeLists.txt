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
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(orcsdr_tab5)
