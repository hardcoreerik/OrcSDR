# OrcSDR application for the Waveshare ESP32-P4-Module-DEV-KIT driving the
# Hosyond 7" 800x480 MIPI-DSI panel through its TC358762 bridge. The panel
# bring-up is the one proved by ../orcsdr-p4-dsi-probe; this project is the
# application that runs on top of it. It stays standalone so a board that is
# still being characterised cannot affect a supported OrcSDR target.
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(orcsdr_p4_hosyond)
