cmake_minimum_required(VERSION 3.16)
# Board support (components/orcsdr_board_hosyond) and the shared adaptive UI core
# (experiments/orcui, recovered from closed PR #197) are separate components so that
# board registers and UI geometry never leak into each other.
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../components"
    "${CMAKE_CURRENT_LIST_DIR}/../../experiments/orcui")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(orcsdr_p4_hosyond_console)
