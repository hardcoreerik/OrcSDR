#include "board_list.h"

/* Waveshare ESP32-P4-ETH (ESP32-P4NRW32X). Status DOCUMENTED, with a few observations from one unit (COM29).
 *
 * The owner identified the unit on COM29 as this model (2026-10-29). Earlier notes in this repository that called
 * it an ESP32-P4-WIFI6-DB with an ESP32-C5 were wrong: that guess came from the 32 MB flash and the missing
 * Ethernet link, and the board simply has no Wi-Fi radio.
 *
 * Source: https://docs.waveshare.com/ESP32-P4-ETH (read 2026-10-29). It states:
 *  - 32 MB NOR flash, 32 MB PSRAM in the package; 100M RJ45 Ethernet with an optional PoE module header;
 *  - no Wi-Fi or Bluetooth and no wireless coprocessor of any kind;
 *  - TF card slot (SDIO 3.0), MIPI-DSI (2 lane) and MIPI-CSI (2 lane) connectors;
 *  - Type-C for power/flash/debug and a 4-pin MX1.25 header carrying the P4's USB 2.0 HS OTG signals; no Type-A;
 *  - microphone, a speaker connector (2 W) and "a codec chip and amplifier chip" that it does not name.
 * The page gives no GPIO table (pins are in an image), does not name the Ethernet PHY and gives no SD power pin.
 *
 * Observed on COM29: ESP32-P4 v3.2, 32 MB flash (GigaDevice c8/4019), an ES8311 answered at I2C 0x18 on SDA7/SCL8,
 * and an RTL-SDR plugged into the USB header enumerated and streamed at 2.4 MS/s with zero drops.
 *
 * Not from the vendor page: the Ethernet pins below are the Waveshare P4 map used by OrcNode (IP101 PHY, MDC31 MDIO52
 * reset51, RMII clock in on 50). A third-party Rust crate (esp-p4-eth) lists the same for this board; neither is the
 * official schematic and neither has been exercised on this unit. The I2S and amplifier pins are the ones the other
 * Waveshare P4 boards use and are unverified here. */
const orcsdr_board_profile_t g_board_waveshare_p4_eth = {
    .name = "waveshare-esp32-p4-eth",
    .vendor = "Waveshare",
    .source = "Waveshare docs ESP32-P4-ETH (2026-10-29); I2C codec and USB host observed on COM29; Ethernet pins from OrcNode",
    .unsupported = "No Wi-Fi or Bluetooth hardware: network access is Ethernet only, and no Ethernet driver is wired into the "
                   "console yet. USB host is a 4-pin header, not a Type-A port. SD power pin unknown; DSI panel, camera, SD, "
                   "audio output and Ethernet untested here.",
    .status = ORCSDR_PROFILE_DOCUMENTED,
    .console_supported = false,

    .i2c_port = 1, .i2c_sda = 7, .i2c_scl = 8,
    .audio_mclk = 13, .audio_bclk = 12, .audio_ws = 10, .audio_dout = 9, .audio_din = 11, .audio_pa_enable = 53,
    .has_sd = true, .sd_power_enable = ORCSDR_PIN_NONE, .sd_power_active_low = false,

    .has_hosted_c6 = false, .wifi_coprocessor = NULL, .c6_reset = ORCSDR_PIN_NONE,
    .c6_sdio_clk = ORCSDR_PIN_NONE, .c6_sdio_cmd = ORCSDR_PIN_NONE, .c6_sdio_d0 = ORCSDR_PIN_NONE,
    .c6_sdio_d1 = ORCSDR_PIN_NONE, .c6_sdio_d2 = ORCSDR_PIN_NONE, .c6_sdio_d3 = ORCSDR_PIN_NONE,

    .has_ethernet = true, .eth_phy = "IP101GRI (unconfirmed for this board)",
    .eth_phy_addr = 1, .eth_phy_reset = 51, .eth_mdc = 31, .eth_mdio = 52,
    .eth_rmii_clk_in = 50, .eth_tx_en = 49, .eth_txd0 = 34, .eth_txd1 = 35,
    .eth_crs_dv = 28, .eth_rxd0 = 29, .eth_rxd1 = 30,

    .has_usb_host_typea = false, .usb_vbus_gpio = ORCSDR_PIN_NONE, .usb_vbus_ioexp = ORCSDR_IOEXP_NONE,

    .ioexp_addr = {0, 0},
    .ioexp_wifi_en = ORCSDR_IOEXP_NONE, .ioexp_lcd_en = ORCSDR_IOEXP_NONE,
    .ioexp_touch_en = ORCSDR_IOEXP_NONE, .ioexp_speaker_en = ORCSDR_IOEXP_NONE,
};
