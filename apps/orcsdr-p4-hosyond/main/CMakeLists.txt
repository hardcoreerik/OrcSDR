idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES esp_lcd esp_driver_i2c esp_hw_support esp_psram esp_hal_lcd esp_timer spi_flash
    EMBED_FILES "orc_badge_104.rgb565"
)
