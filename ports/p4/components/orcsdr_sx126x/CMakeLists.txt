idf_component_register(SRCS "orcsdr_sx126x.c" "vendor/sx126x.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "vendor"
    REQUIRES orcsdr_board_hosyond esp_driver_spi esp_driver_gpio esp_timer esp_rom freertos)
