idf_component_register(
    SRCS "src/orcsdr_c6_rom_update.c"
         "src/loader_write_policy.c"
         "src/loader_protocol_guard.c"
         "src/loader_flash.c"
         "src/loader_connect.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES esp_serial_flasher mbedtls esp_driver_sdmmc esp_driver_gpio
)

idf_component_get_property(FLASHER_DIR esp_serial_flasher COMPONENT_DIR)
target_include_directories(${COMPONENT_LIB} PRIVATE "${FLASHER_DIR}/private_include")
target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_loader_get_sdio_ops"
    "-Wl,--undefined=__wrap_esp_loader_get_sdio_ops")
idf_component_get_property(FLASHER_LIB esp_serial_flasher COMPONENT_LIB)
target_compile_definitions(${FLASHER_LIB} PRIVATE SERIAL_FLASHER_WRITE_BLOCK_RETRIES=1)
