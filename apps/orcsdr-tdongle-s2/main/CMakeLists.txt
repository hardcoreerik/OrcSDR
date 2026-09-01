idf_component_register(SRCS "main.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES esp_rtl_sdr_s2 nvs_flash esp_lcd driver)
