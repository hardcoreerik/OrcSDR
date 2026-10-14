idf_component_register(SRCS "main.cpp" INCLUDE_DIRS "."
    REQUIRES lvgl esp_rtl_sdr esp_psram)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
