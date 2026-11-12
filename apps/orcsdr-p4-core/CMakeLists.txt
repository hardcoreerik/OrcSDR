cmake_minimum_required(VERSION 3.16)
# OrcSDR P4 Core: the firmware for ESP32-P4 boards. Board support (components/orcsdr_board_hosyond) is a separate component so that
# board registers and UI geometry never leak into each other.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(orcsdr_p4_core)
