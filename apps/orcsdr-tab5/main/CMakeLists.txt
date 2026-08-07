idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_driver_usb_serial_jtag
        esp_hw_support
        esp_psram
        esp_system
        esp_timer
        spi_flash
)
