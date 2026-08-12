cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")
set(ENV{EXTRA_CPPFLAGS}
    "$ENV{EXTRA_CPPFLAGS} -Wno-error=stringop-truncation -Wno-error=maybe-uninitialized")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(orcsdr_tab5)
