#include "board_list.h"

/* Waveshare ESP32-P4-Module-DEV-KIT. Sources: the Waveshare schematic and wiki
 * (https://docs.waveshare.com/ESP32-P4-Module-DEV-KIT), Espressif's espp BSP, and the OrcNode firmware
 * (F:\Ai\OrcNode, main/network_eth.cpp and sdkconfig.defaults), whose PROJECT_TRUTH.md marks this board's
 * Ethernet (IP101 RMII), ESP-Hosted SDIO, SD (4-bit, LDO channel 4) and Wi-Fi as hardware-verified.
 *
 * Exercised with THIS firmware on the bench with the Hosyond 7" DSI panel (docs/HOSYOND_7IN_DSI_P4.md):
 * I2C, DSI display and touch, the ES8311 audio path, the C6 SDIO link and the USB-A host with an RTL-SDR.
 * NOT exercised with this firmware: the microSD slot (no card was ever mounted), Ethernet. The GPIO45 SD power
 * switch below comes from the schematic; OrcNode instead powers the slot from the P4's LDO channel 4.
 * The status is bench-verified for the display/audio/USB/C6 path, not for every field. */
const orcsdr_board_profile_t g_board_waveshare_p4_module_dev_kit = {
    .name = "waveshare-esp32-p4-module-dev-kit",
    .vendor = "Waveshare",
    .source = "Waveshare schematic + wiki; espp BSP; OrcNode hardware-verified Ethernet/SDIO/SD pins; "
              "display, audio, C6 and USB bench-exercised with this firmware (2026-10)",
    .unsupported = "Ethernet is present but not wired into the console. SD slot never mounted a card here. "
                   "USB host VBUS is set by a hardware jumper, not software.",
    .status = ORCSDR_PROFILE_BENCH_VERIFIED,
    .console_supported = true,

    .i2c_port = 1, .i2c_sda = 7, .i2c_scl = 8,
    .audio_mclk = 13, .audio_bclk = 12, .audio_ws = 10, .audio_dout = 9, .audio_din = 11, .audio_pa_enable = 53,
    .has_sd = true, .sd_power_enable = 45, .sd_power_active_low = true,

    .has_hosted_c6 = true, .wifi_coprocessor = "ESP32-C6", .c6_reset = 54,
    .c6_sdio_clk = 18, .c6_sdio_cmd = 19, .c6_sdio_d0 = 14, .c6_sdio_d1 = 15, .c6_sdio_d2 = 16, .c6_sdio_d3 = 17,

    /* OrcNode main/network_eth.cpp: IP101 at PHY address 1, RMII clock input on GPIO50, reset GPIO51 (active low). */
    .has_ethernet = true, .eth_phy = "IP101",
    .eth_phy_addr = 1, .eth_phy_reset = 51, .eth_mdc = 31, .eth_mdio = 52,
    .eth_rmii_clk_in = 50, .eth_tx_en = 49, .eth_txd0 = 34, .eth_txd1 = 35, .eth_crs_dv = 28, .eth_rxd0 = 29, .eth_rxd1 = 30,

    .has_usb_host_typea = true, .usb_vbus_gpio = ORCSDR_PIN_NONE, .usb_vbus_ioexp = ORCSDR_IOEXP_NONE,

    .ioexp_addr = {0, 0},
    .ioexp_wifi_en = ORCSDR_IOEXP_NONE, .ioexp_lcd_en = ORCSDR_IOEXP_NONE,
    .ioexp_touch_en = ORCSDR_IOEXP_NONE, .ioexp_speaker_en = ORCSDR_IOEXP_NONE,

    .codec_i2c_port = ORCSDR_PIN_NONE, .codec_i2c_sda = ORCSDR_PIN_NONE, .codec_i2c_scl = ORCSDR_PIN_NONE,
    .has_radio = false, .radio_part = NULL,
    .radio_sclk = ORCSDR_PIN_NONE, .radio_mosi = ORCSDR_PIN_NONE, .radio_miso = ORCSDR_PIN_NONE,
    .radio_cs = ORCSDR_PIN_NONE, .radio_busy = ORCSDR_PIN_NONE, .radio_spi_hz = 0,
    .radio_rst = ORCSDR_IOEXP_NONE, .radio_dio1 = ORCSDR_IOEXP_NONE, .radio_ant_sw = ORCSDR_IOEXP_NONE,
    .ioexp_sd_power = ORCSDR_IOEXP_NONE, .ioexp_screen_rst = ORCSDR_IOEXP_NONE, .ioexp_touch_rst = ORCSDR_IOEXP_NONE,
    .ioexp_usb_phy_en = ORCSDR_IOEXP_NONE, .ioexp_eth_rst = ORCSDR_IOEXP_NONE,
};
