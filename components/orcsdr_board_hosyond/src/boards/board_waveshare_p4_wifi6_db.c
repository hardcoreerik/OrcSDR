#include "board_list.h"

/* Waveshare ESP32-P4-WIFI6-DB (ESP32-P4NRW32X). Status DOCUMENTED, never run here with its coprocessor.
 *
 * Source: Waveshare's wiki page https://docs.waveshare.com/ESP32-P4-WIFI6-DB (read 2026-10-29). It states:
 *  - Wi-Fi coprocessor is an ESP32-C5 (dual-band Wi-Fi 6) on SDIO: CLK GPIO18, CMD GPIO19, D0-D3 GPIO14-17;
 *    C5_CHIP_PU on GPIO54 ("high to enable, low to reset"); GPIO6 is the C5 wake-up pin; the C5 UART is on pads.
 *  - 32 MB NOR flash, 32 MB PSRAM in the package; no Ethernet port.
 *  - microSD 4-bit on GPIO39-42, CLK 43, CMD 44, power control GPIO45 (low = enabled).
 *  - ES8311 codec at I2C 0x18, I2S on GPIO9-13, amplifier enable GPIO53 (active high), BOOT key on GPIO35.
 *  - Type-C for power/flash/debug and a 4-pin header (MX1.25) carrying the P4's USB 2.0 HS OTG signals; no Type-A.
 *  - 2-lane MIPI-DSI (5/7/8/10.1" screens) and 2-lane MIPI-CSI connectors.
 * The page gives no I2C pin numbers, PCB revision or chip revision, and names no ESP-Hosted C5 image.
 *
 * Observed here on one board believed to be this model (ESP32-P4 v3.2, 32 MB flash): the ES8311 answered at 0x18 on
 * I2C SDA7/SCL8 and an RTL-SDR enumerated and streamed through the USB host, so those two are hardware-observed.
 * Everything else, including the C5 link, is documented only. The I2S/amplifier assignments below are the ones the
 * other Waveshare P4 boards use; the wiki lists the GPIO range, not each signal.
 *
 * Bench result, a real unit (COM31, owner-identified WIFI6-DB, ESP32-P4 v3.2, 2026-10-30): the SDIO link to the C5 comes up
 * with this profile's pins and active-low reset ("Card init success", "slave chip id: 0x17 (esp32c5)"). The C5 ships with an
 * ESP-Hosted slave, firmware 2.12.9; this firmware's host is 3.0.6, a major-version mismatch, so the host asks to OTA the
 * coprocessor and Wi-Fi stays down (stage hosted_connect). Our embedded image is the ESP32-C6 build and is never offered to a C5.
 * Ways forward, none done: host-driven OTA of an ESP-Hosted 3.0.x C5 slave image over the existing SDIO link, or flashing it
 * through the C5 UART pads. A 3.0.6 C5 slave image builds (see docs/hardware/P4_BOARD_PROFILES.md) but has never run. */
const orcsdr_board_profile_t g_board_waveshare_p4_wifi6_db = {
    .name = "waveshare-esp32-p4-wifi6-db",
    .vendor = "Waveshare",
    .source = "Waveshare wiki ESP32-P4-WIFI6-DB (2026-10-29); I2C 0x18 codec and USB host observed on one board",
    .unsupported = "Wi-Fi: the C5 runs ESP-Hosted slave 2.12.9 and this host is 3.0.6 (major mismatch); it needs a 3.0.x C5 slave "
                   "image (the embedded one is C6-only and is never offered to it). "
                   "USB host is a 4-pin header, not a Type-A port. DSI panel, camera, SD and audio output untested here.",
    .status = ORCSDR_PROFILE_DOCUMENTED,
    .console_supported = false,

    .i2c_port = 1, .i2c_sda = 7, .i2c_scl = 8,
    .audio_mclk = 13, .audio_bclk = 12, .audio_ws = 10, .audio_dout = 9, .audio_din = 11, .audio_pa_enable = 53,
    .has_sd = true, .sd_power_enable = 45, .sd_power_active_low = true,

    .has_hosted_c6 = true, .wifi_coprocessor = "ESP32-C5", .c6_reset = 54,
    .c6_sdio_clk = 18, .c6_sdio_cmd = 19, .c6_sdio_d0 = 14, .c6_sdio_d1 = 15, .c6_sdio_d2 = 16, .c6_sdio_d3 = 17,

    .has_ethernet = false, .eth_phy = NULL,
    .eth_phy_addr = ORCSDR_PIN_NONE, .eth_phy_reset = ORCSDR_PIN_NONE, .eth_mdc = ORCSDR_PIN_NONE, .eth_mdio = ORCSDR_PIN_NONE,
    .eth_rmii_clk_in = ORCSDR_PIN_NONE, .eth_tx_en = ORCSDR_PIN_NONE, .eth_txd0 = ORCSDR_PIN_NONE, .eth_txd1 = ORCSDR_PIN_NONE,
    .eth_crs_dv = ORCSDR_PIN_NONE, .eth_rxd0 = ORCSDR_PIN_NONE, .eth_rxd1 = ORCSDR_PIN_NONE,

    .has_usb_host_typea = false, .usb_vbus_gpio = ORCSDR_PIN_NONE, .usb_vbus_ioexp = ORCSDR_IOEXP_NONE,

    .ioexp_addr = {0, 0},
    .ioexp_wifi_en = ORCSDR_IOEXP_NONE, .ioexp_lcd_en = ORCSDR_IOEXP_NONE,
    .ioexp_touch_en = ORCSDR_IOEXP_NONE, .ioexp_speaker_en = ORCSDR_IOEXP_NONE,
};
