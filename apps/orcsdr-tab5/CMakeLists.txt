cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")
set(ENV{EXTRA_CPPFLAGS}
    "$ENV{EXTRA_CPPFLAGS} -Wno-error=stringop-truncation -Wno-error=maybe-uninitialized -DARDUINO_M5STACK_TAB5=1 -DBOARD_HAS_PSRAM=1 -DARDUINO_USB_CDC_ON_BOOT=0 -DARDUINO_USB_MODE=1 -DCORE_DEBUG_LEVEL=4 -DRTL_USE_LEGACY_USB=0")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(orcsdr_tab5)
