# Only the panel bring-up code lives here; everything is compiled into the app.
# esp_hal_lcd is an ESP-IDF 6.x component; on 5.5.x the same HAL headers are in `hal`.
set(PROBE_REQUIRES esp_lcd esp_driver_i2c esp_hw_support esp_psram esp_timer)
if(IDF_VERSION_MAJOR GREATER_EQUAL 6)
    list(APPEND PROBE_REQUIRES esp_hal_lcd)
else()
    list(APPEND PROBE_REQUIRES hal)
endif()
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES ${PROBE_REQUIRES}
)
