idf_component_register(
    SRCS
        "../ui/main.cpp"
        "../ui/orcsdr_splash.cpp"
        "../ui/adsb_dashboard.cpp"
        "../ui/adsb_decoder.cpp"
        "../ui/fm_dashboard.cpp"
        "../ui/settings_app.cpp"
    INCLUDE_DIRS "." "../ui"
    REQUIRES
        arduino-esp32
        m5unified
        m5gfx
        nvs_flash
        rtl_sdr_v4_esp
        esp_driver_usb_serial_jtag
        esp_driver_jpeg
        esp_hw_support
        esp_psram
        esp_system
        esp_timer
        spi_flash
        usb
    EMBED_FILES
        "orc_badge_104.png"
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
