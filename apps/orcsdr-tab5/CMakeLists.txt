cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")
set(ENV{EXTRA_CPPFLAGS}
    "$ENV{EXTRA_CPPFLAGS} -Wno-error=stringop-truncation -Wno-error=maybe-uninitialized -DARDUINO_M5STACK_TAB5=1 -DBOARD_HAS_PSRAM=1 -DARDUINO_USB_CDC_ON_BOOT=0 -DARDUINO_USB_MODE=1 -DCORE_DEBUG_LEVEL=4 -DRTL_USE_LEGACY_USB=0")
# Data-pack tags must not become the application firmware version.  Keep the
# catalog compatibility gate tied to the most recent semantic firmware tag.
execute_process(
    COMMAND git describe --tags --match "v[0-9]*"
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../.."
    OUTPUT_VARIABLE ORCSDR_GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
string(REGEX REPLACE "^(v[0-9]+\\.[0-9]+\\.[0-9]+(-alpha\\.[0-9]+)?).*$" "\\1"
       PROJECT_VER "${ORCSDR_GIT_VERSION}")
if (NOT PROJECT_VER MATCHES "^v[0-9]+\\.[0-9]+\\.[0-9]+")
    set(PROJECT_VER "v0.0.0-alpha.0")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(orcsdr_tab5)
