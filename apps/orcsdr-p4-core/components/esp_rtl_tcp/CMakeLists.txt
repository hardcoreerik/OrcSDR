# esp-rtl-tcp: an rtl_tcp-compatible IQ server for ESP32 boards. Application layer, written from the observed behaviour of rtl_tcp
# (docs/hardware/ESP_RTL_TCP_DESIGN.md); it has no dependency on OrcSDR or on the esp-rtl-sdr driver. The protocol core
# (include/rtl_tcp_proto.hpp) is pure C++ and host-tested (tests/).
idf_component_register(
    SRCS "src/esp_rtl_tcp.cpp"
    INCLUDE_DIRS "include"
    REQUIRES lwip freertos log esp_timer
)
