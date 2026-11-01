cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../ports/p4/components"
    "${CMAKE_CURRENT_LIST_DIR}/../../components")
set(COMPONENTS main)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(orcsdr_p4_hosyond_console)
include("${CMAKE_CURRENT_LIST_DIR}/../../ports/p4/hosted/board_reset.cmake")
