#include "board_list.h"

/* Waveshare ESP32-P4-WIFI6-POE-ETH. Status DOCUMENTED, never run here.
 *
 * Source: the espp component readme for this board
 * (https://components.espressif.com/components/espp/esp32-p4-wifi6-poe-eth/versions/1.3.7/readme), which
 * restates Waveshare's schematic. That is a third-party summary, not the schematic itself, and it names no
 * board revision. Before relying on this profile: read the schematic PDF linked from the Waveshare product
 * page, confirm the silkscreen revision, and bench-check each group below.
 *
 * Differences from the Module-DEV-KIT that matter: the microSD slot has no power switch pin (its pull-ups
 * come from the P4's LDO_VO4), so sd_power_enable is -1 and the slot is still present; the board has a
 * 100 Mbps Ethernet port (IP101GRI) and PoE input.
 *
 * Not established by that source: the USB-A VBUS switch, whether the DSI connector wiring and I2C
 * addresses match the dev kit, and the ESP-Hosted C6 firmware version. */
const orcsdr_board_profile_t g_board_waveshare_p4_wifi6_poe_eth = {
    .name = "waveshare-esp32-p4-wifi6-poe-eth",
    .vendor = "Waveshare",
    .source = "espp component readme v1.3.7 (restates Waveshare schematic); not bench-verified",
    .unsupported = "Ethernet driver not wired in; USB-A VBUS control unverified; DSI panel and touch untested "
                   "on this board; ESP-Hosted C6 firmware version unknown.",
    .status = ORCSDR_PROFILE_DOCUMENTED,
    .console_supported = false,

    .i2c_port = 1, .i2c_sda = 7, .i2c_scl = 8,
    .audio_mclk = 13, .audio_bclk = 12, .audio_ws = 10, .audio_dout = 9, .audio_din = 11, .audio_pa_enable = 53,
    .has_sd = true, .sd_power_enable = ORCSDR_PIN_NONE, .sd_power_active_low = false,

    .has_hosted_c6 = true, .wifi_coprocessor = "ESP32-C6", .c6_reset = 54,
    .c6_sdio_clk = 18, .c6_sdio_cmd = 19, .c6_sdio_d0 = 14, .c6_sdio_d1 = 15, .c6_sdio_d2 = 16, .c6_sdio_d3 = 17,

    /* Same RMII map the OrcNode firmware runs on the Module-DEV-KIT (main/network_eth.cpp), which matches the espp readme. */
    .has_ethernet = true, .eth_phy = "IP101GRI",
    .eth_phy_addr = 1, .eth_phy_reset = 51, .eth_mdc = 31, .eth_mdio = 52,
    .eth_rmii_clk_in = 50, .eth_tx_en = 49, .eth_txd0 = 34, .eth_txd1 = 35, .eth_crs_dv = 28, .eth_rxd0 = 29, .eth_rxd1 = 30,

    .has_usb_host_typea = true, .usb_vbus_gpio = ORCSDR_PIN_NONE, .usb_vbus_ioexp = ORCSDR_IOEXP_NONE,

    .ioexp_addr = {0, 0},
    .ioexp_wifi_en = ORCSDR_IOEXP_NONE, .ioexp_lcd_en = ORCSDR_IOEXP_NONE,
    .ioexp_touch_en = ORCSDR_IOEXP_NONE, .ioexp_speaker_en = ORCSDR_IOEXP_NONE,
};
