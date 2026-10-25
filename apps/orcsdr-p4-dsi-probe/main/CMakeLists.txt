# Only the panel bring-up code lives here; everything is compiled into the app.
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES esp_lcd esp_driver_i2c esp_hw_support esp_psram esp_hal_lcd
)
