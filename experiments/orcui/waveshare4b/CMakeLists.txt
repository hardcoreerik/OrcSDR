cmake_minimum_required(VERSION 3.16)
# Standalone Waveshare P4 4B proof application; intentionally separate from Tab5.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/..")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(orcui_waveshare4b)
