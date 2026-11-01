cmake_minimum_required(VERSION 3.16)
# Keep the board-specific M5Unified shim out of the native Tab5 target's search path.
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../ports/p4/components/orcsdr_board_hosyond"
    "${CMAKE_CURRENT_LIST_DIR}/../../ports/p4/components/orcsdr_gfx_compat"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/orcsdr_display_geometry")
set(COMPONENTS main)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(orcsdr_p4_display_probe)
