# RTL-SDRv4-ESP — standalone ESP-IDF component
#
# Requires ESP-IDF USB Host (esp_usb). High-Speed host is required for the
# measured Blog V4 path on ESP32-P4. Full-Speed targets are not claimed until
# measured.

idf_component_register(
    SRCS "src/rtl_sdr_v4_esp.cpp"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private"
    # `usb` is required once bulk host path is extracted (Gate 2).
    REQUIRES log esp_timer
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
