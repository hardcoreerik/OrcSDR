# Board support for the Waveshare ESP32-P4-Module-DEV-KIT with the Hosyond 7" 800x480
# MIPI-DSI touchscreen (TC358762 bridge + ATTINY88 + FT5x06). Panel register writes,
# pins, reset sequencing and the touch controller live here and nowhere else.
# Bring-up evidence: docs/HOSYOND_7IN_DSI_P4.md. Builds on ESP-IDF 5.5.x and 6.x.
set(HOSYOND_REQUIRES esp_lcd esp_driver_i2c esp_hw_support esp_psram esp_timer esp_mm)
if(IDF_VERSION_MAJOR GREATER_EQUAL 6)
    list(APPEND HOSYOND_REQUIRES esp_hal_lcd)
else()
    list(APPEND HOSYOND_REQUIRES hal)
endif()
idf_component_register(
    SRCS "src/hosyond_board.c" "src/ft5x06_touch.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES ${HOSYOND_REQUIRES}
)
