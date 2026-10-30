#include "board_list.h"

/* LilyGO T-Display-P4 V1.0, 4.1" AMOLED assembly, SX1262 radio.
 * Status DOCUMENTED. This firmware has not been run on the board.
 *
 * The owner unit is the AMOLED SKU (RM69A10 + GT9895), not the 4.05" HI8561 TFT.
 * The same PCB can be built with either panel and with either SX1262 or LR2021; this profile
 * is the AMOLED + SX1262 combination only. The TFT is a different panel and is not described here.
 *
 * Pin source: Xinyuan-LilyGO/lilygo_device_driver
 * src/device/t_display_p4/t_display_p4_config.h at commit a47cf73e2c9d442c0f00b618749150f4482dcd0a
 * (GPL-3.0). Numbers below are that header's constants, not a copy of its driver. Panel identity
 * and the coprocessor part are from Xinyuan-LilyGO/T-Display-P4 README.md at 7b021f79
 * (hardware table: ESP32-P4 16 MB flash, ESP32-C6, RM69A10 / GT9895 568x1232, SX1262 or LR2021).
 *
 * Panel bus (I2C1): SDA 7, SCL 8. On that bus the header puts XL9535 at 0x20 and GT9895 touch at
 * 0x5D. HI8561 touch (0x68) belongs to the TFT assembly and is not this profile.
 * Codec bus (I2C0): SDA 20, SCL 21. ES8311 is at 0x18 on this second bus, not on the panel bus.
 * ICM20948 (0x68) is also on the codec bus, so a 0x68 answer there is the IMU, not a touch controller.
 *
 * Audio I2S, from the same header: MCLK 13, BCLK 12, WS 9, DOUT 10 (P4 -> codec), DIN 11.
 * WS and DOUT are swapped relative to the Waveshare P4 boards. Amplifier power is XL9535 IO6
 * (kAudioPowerEn), not a GPIO, so audio_pa_enable is absent.
 *
 * C6 SDIO: CLK 18, CMD 19, D0-D3 14-17. Enable is XL9535 IO14 (kEsp32c6En), not a GPIO, so
 * c6_reset is absent and ioexp_wifi_en carries it. ESP-Hosted's reset GPIO must not be treated
 * as this enable. Wake is XL9535 IO13 and is unused here.
 *
 * SX1262 SPI (the LR2021 build uses the same pins; this profile names SX1262 because that is
 * the unit on order): SCLK 2, MOSI 3, MISO 4, CS 24, BUSY 6, 10 MHz. Reset XL9535 IO16, DIO1
 * XL9535 IO17. Antenna switch is SKY13453 VCTL on XL9535 IO1. Polarity is not encoded: do not
 * drive radio_ant_sw until the switch sense is confirmed on the board.
 *
 * microSD is SDMMC on CLK 43, CMD 44, D0-D3 39-42. Slot power is XL9535 IO15, not a GPIO.
 * Active level is not confirmed, so the console must not switch it yet.
 *
 * Ethernet is IP101 at PHY address 1. RMII: clock-in 50, MDC 31, MDIO 52, TX_EN 49, TXD0 34,
 * TXD1 35, CRS_DV 28, RXD0 29, RXD1 30. PHY reset is XL9535 IO5, not a GPIO.
 *
 * USB: XL9535 IO10 is named USB PHY power enable. It is not a confirmed 5 V VBUS switch, and
 * this profile does not claim a Type-A host port. RTL-SDR enumeration is unverified.
 *
 * AMOLED timings from the same header's rm69a10 namespace, recorded for the display backend
 * that does not exist yet: 568x1232, 2 lanes, 1000 Mbps, DPI clock 60 MHz, hsync 50, hbp 150,
 * hfp 50, vsync 40, vbp 120, vfp 80. GT9895 raw coordinates are 1060x2400, not the panel size.
 * The console's only display backend is the Hosyond TC358762 bridge at 800x480, so
 * console_supported stays false and a GT9895 probe must not select that backend.
 *
 * Also on the board and unused here: L76K GNSS UART TX 22 / RX 23 (wake XL9535 IO11),
 * PCF8563 RTC, BQ27220 gauge, AW86224 haptic, camera, BOOT key GPIO35. */
const orcsdr_board_profile_t g_board_lilygo_t_display_p4_amoled = {
    .name = "lilygo-t-display-p4-amoled",
    .vendor = "LilyGO",
    .source = "lilygo_device_driver t_display_p4_config.h @ a47cf73e; T-Display-P4 README @ 7b021f79; not run",
    .unsupported = "No RM69A10/GT9895 display backend (console stays headless). Expander outputs are not "
                   "driven: C6 enable, USB PHY power, speaker power, SD power, screen/touch reset and the "
                   "SX1262 reset all sit on the XL9535. Onboard SX1262 is described, not used. Ethernet, "
                   "GNSS, RTC, IMU, haptic, camera and the battery gauge are unused. RTL-SDR host power "
                   "is unverified. Do not flash until the silkscreen says AMOLED and the silicon revision is read.",
    .status = ORCSDR_PROFILE_DOCUMENTED,
    .console_supported = false,

    .i2c_port = 1, .i2c_sda = 7, .i2c_scl = 8,
    .audio_mclk = 13, .audio_bclk = 12, .audio_ws = 9, .audio_dout = 10, .audio_din = 11,
    .audio_pa_enable = ORCSDR_PIN_NONE,
    .has_sd = true, .sd_power_enable = ORCSDR_PIN_NONE, .sd_power_active_low = false,

    .has_hosted_c6 = true, .wifi_coprocessor = "ESP32-C6", .c6_reset = ORCSDR_PIN_NONE,
    .c6_sdio_clk = 18, .c6_sdio_cmd = 19, .c6_sdio_d0 = 14, .c6_sdio_d1 = 15, .c6_sdio_d2 = 16, .c6_sdio_d3 = 17,

    .has_ethernet = true, .eth_phy = "IP101",
    .eth_phy_addr = 1, .eth_phy_reset = ORCSDR_PIN_NONE, .eth_mdc = 31, .eth_mdio = 52,
    .eth_rmii_clk_in = 50, .eth_tx_en = 49, .eth_txd0 = 34, .eth_txd1 = 35, .eth_crs_dv = 28,
    .eth_rxd0 = 29, .eth_rxd1 = 30,

    .has_usb_host_typea = false, .usb_vbus_gpio = ORCSDR_PIN_NONE, .usb_vbus_ioexp = ORCSDR_IOEXP_NONE,

    .ioexp_addr = {0x20, 0},
    .ioexp_wifi_en = {0, 14}, .ioexp_lcd_en = ORCSDR_IOEXP_NONE,
    .ioexp_touch_en = ORCSDR_IOEXP_NONE, .ioexp_speaker_en = {0, 6},

    .codec_i2c_port = 0, .codec_i2c_sda = 20, .codec_i2c_scl = 21,
    .has_radio = true, .radio_part = "SX1262",
    .radio_sclk = 2, .radio_mosi = 3, .radio_miso = 4, .radio_cs = 24, .radio_busy = 6,
    .radio_spi_hz = 10000000,
    .radio_rst = {0, 16}, .radio_dio1 = {0, 17}, .radio_ant_sw = {0, 1},
    .ioexp_sd_power = {0, 15}, .ioexp_screen_rst = {0, 2}, .ioexp_touch_rst = {0, 3},
    .ioexp_usb_phy_en = {0, 10}, .ioexp_eth_rst = {0, 5},
};
