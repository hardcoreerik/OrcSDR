#include "board_list.h"

/* Waveshare ESP32-P4-Module-DEV-KIT. Sources: the Waveshare schematic and wiki
 * (https://docs.waveshare.com/ESP32-P4-Module-DEV-KIT) and Espressif's espp BSP. The SD, audio and C6
 * pins have been exercised on the bench with this firmware and the Hosyond 7" DSI panel
 * (docs/HOSYOND_7IN_DSI_P4.md); the USB-A host port ran an RTL-SDR dongle (jumper-selected OTG/host). */
const orcsdr_board_profile_t g_board_waveshare_p4_module_dev_kit = {
    .name = "waveshare-esp32-p4-module-dev-kit",
    .vendor = "Waveshare",
    .source = "Waveshare schematic + wiki; espp BSP; bench-exercised with this firmware (2026-10)",
    .unsupported = "Ethernet absent. USB host VBUS is set by a hardware jumper, not software.",
    .status = ORCSDR_PROFILE_BENCH_VERIFIED,
    .console_supported = true,

    .i2c_port = 1, .i2c_sda = 7, .i2c_scl = 8,
    .audio_mclk = 13, .audio_bclk = 12, .audio_ws = 10, .audio_dout = 9, .audio_din = 11, .audio_pa_enable = 53,
    .has_sd = true, .sd_power_enable = 45, .sd_power_active_low = true,

    .has_hosted_c6 = true, .c6_reset = 54,
    .c6_sdio_clk = 18, .c6_sdio_cmd = 19, .c6_sdio_d0 = 14, .c6_sdio_d1 = 15, .c6_sdio_d2 = 16, .c6_sdio_d3 = 17,

    .has_ethernet = false, .eth_phy = NULL,

    .has_usb_host_typea = true, .usb_vbus_gpio = ORCSDR_PIN_NONE, .usb_vbus_ioexp = ORCSDR_IOEXP_NONE,

    .ioexp_addr = {0, 0},
    .ioexp_wifi_en = ORCSDR_IOEXP_NONE, .ioexp_lcd_en = ORCSDR_IOEXP_NONE,
    .ioexp_touch_en = ORCSDR_IOEXP_NONE, .ioexp_speaker_en = ORCSDR_IOEXP_NONE,
};
