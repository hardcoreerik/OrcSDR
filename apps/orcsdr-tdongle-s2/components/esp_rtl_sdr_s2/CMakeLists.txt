# The pinned upstream component manifest correctly declares ESP32-P4 only.
# This bench compiles the same sources without changing that upstream claim.
set(ESP_RTL_SDR_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../../third_party/esp_rtl_sdr")

idf_component_register(
    SRCS "${ESP_RTL_SDR_SOURCE}/src/esp_rtl_sdr.cpp"
         "${ESP_RTL_SDR_SOURCE}/src/esp_rtl_sdr_policy.cpp"
    INCLUDE_DIRS "${ESP_RTL_SDR_SOURCE}/include"
    PRIV_INCLUDE_DIRS "${ESP_RTL_SDR_SOURCE}/private"
    REQUIRES log esp_timer freertos usb
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
# ESP-IDF 6 compiles as C++26 and promotes the upstream 0.7.12 volatile
# increment deprecation to an error. Keep this compatibility waiver local.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=volatile)
