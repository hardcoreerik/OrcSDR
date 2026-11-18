cmake_minimum_required(VERSION 3.16)
# A separate LilyGO ESP-IDF project. Reuse the existing ESP32-P4 console application
# component until the dedicated RM69A10/GT9895 backend is ready. Do not copy/fork
# its UI, DSP or USB receiver implementation into this application.
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../components"
    "${CMAKE_CURRENT_LIST_DIR}/../../experiments/orcui"
    "${CMAKE_CURRENT_LIST_DIR}/../orcsdr-p4-hosyond-console/main")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(orcsdr_lilygo_p4)

# Fail closed: a default/incorrect board configuration must never build a
# misleading LilyGO image. The profile is selected in sdkconfig.defaults.
if(NOT CONFIG_ORCSDR_BOARD_LILYGO_T_DISPLAY_P4_AMOLED)
    message(FATAL_ERROR "LilyGO app requires CONFIG_ORCSDR_BOARD_LILYGO_T_DISPLAY_P4_AMOLED=y. Use tools/lilygo.py.")
endif()
