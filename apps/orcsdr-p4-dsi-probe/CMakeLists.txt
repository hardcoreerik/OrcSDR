# Isolated hardware diagnostic for the Hosyond 7" 800x480 MIPI-DSI panel on the
# Waveshare ESP32-P4-Module-DEV-KIT. Deliberately standalone: it shares no code
# with orcsdr-tab5 / orcsdr-waveshare-4b / OrcNode, so a failed experiment here
# cannot change any supported OrcSDR target.
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(orcsdr_p4_dsi_probe)
