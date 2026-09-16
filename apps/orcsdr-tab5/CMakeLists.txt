cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

# OrcMaps is a separate library with its own versioning, consumed as an
# ESP-IDF component exactly like esp_rtl_sdr. It is resolved from a local
# checkout here ONLY because the OrcMaps commits this build needs are not
# published yet; a git: + full-SHA pin in main/idf_component.yml replaces
# this as soon as they are, and nothing else about the integration changes.
#
# ESP-IDF names a component after its directory, so the name is derived
# rather than hardcoded and the checkout may be called anything.
if(NOT ORCMAP_DIR AND DEFINED ENV{ORCMAP_DIR})
    set(ORCMAP_DIR "$ENV{ORCMAP_DIR}")
endif()
if(NOT ORCMAP_DIR)
    get_filename_component(ORCMAP_DIR
        "${CMAKE_CURRENT_LIST_DIR}/../../../../OrcMaps" ABSOLUTE)
endif()
if(EXISTS "${ORCMAP_DIR}/CMakeLists.txt")
    list(APPEND EXTRA_COMPONENT_DIRS "${ORCMAP_DIR}")
    get_filename_component(_orcmap_name "${ORCMAP_DIR}" NAME)
    # CACHE INTERNAL: component CMakeLists are processed in their own scope,
    # so a plain variable set here is empty by the time main/ reads it -- which
    # silently produces a component that requires nothing and an include path
    # that is never added.
    set(ORCMAP_COMPONENT "${_orcmap_name}" CACHE INTERNAL "OrcMaps component name")
    # Component CMakeLists are expanded in their own scope and early enough
    # that neither a plain variable nor a freshly written cache entry is
    # reliably visible, which silently yields a component requiring nothing.
    # The environment is process-global, and this file already uses it for
    # EXTRA_CPPFLAGS.
    set(ENV{ORCMAP_COMPONENT} "${_orcmap_name}")
    message(STATUS "OrcMaps component '${ORCMAP_COMPONENT}' from ${ORCMAP_DIR}")
else()
    message(FATAL_ERROR
        "OrcMaps checkout not found at ${ORCMAP_DIR}. "
        "Pass -DORCMAP_DIR=<path> or set ORCMAP_DIR in the environment.")
endif()
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
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(orcsdr_tab5)
