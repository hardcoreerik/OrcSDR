cmake_minimum_required(VERSION 3.16)

# Resolve the monorepo component without installing globally.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(p4_serial_smoke)
