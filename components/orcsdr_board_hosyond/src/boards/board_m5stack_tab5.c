#include "board_list.h"

/* M5Stack Tab5 (ESP32-P4). Status DOCUMENTED for this firmware: nothing here has been run on a Tab5 by this
 * console app. The Tab5's own firmware (apps/orcsdr-tab5) is a separate product and is not touched.
 *
 * Sources:
 *  - Espressif esp-bsp, bsp/m5stack_tab5 at commit f259a6ab (2026-10-08), Apache-2.0:
 *    include/bsp/m5stack_tab5.h (I2C 31/32, I2S 30/27/29/26/28, expander lines, SD 39-44),
 *    src/bsp_feature_en.c (which expander carries which enable), src/bsp_display.c (two board versions).
 *  - apps/orcsdr-tab5/sdkconfig.defaults, which runs on hardware: C6 SDIO CLK12 CMD13 D0=11 D1=10 D2=9 D3=8,
 *    reset GPIO15 active high. apps/orcsdr-tab5/ui/main.cpp uses expander 0x44 bit 0 as WLAN_PWR_EN.
 * Expander addresses: the BSP names them ADDRESS_LOW and ADDRESS_HIGH of the PI4IOE5V6408 (0x43 and 0x44);
 * 0x44 is confirmed by main.cpp, 0x43 is the part's other address and is not separately confirmed.
 *
 * Why console_supported is false: the Tab5 panel is a native 720x1280 DSI panel driven by esp_lcd drivers and
 * chosen at boot by probing the touch address. At the commit read here (f259a6ab) there were two assemblies,
 * ILI9881C + GT911 and ST7123 + ST7123 touch. The research branch (HANDOFF/SOURCE_CONFLICTS C05, pinned BSP commit
 * 9112bd33, not re-read by this file's author) reports a THIRD path, ST7121, so detect the assembly at runtime and
 * never hardcode one init table. The console's display path is the TC358762 bridge used by the Hosyond panel, so a
 * Tab5 needs a second display backend (see docs/hardware/P4_BOARD_PROFILES.md). Do not flash a Tab5 with the
 * console firmware: it has no display driver for it. */
const orcsdr_board_profile_t g_board_m5stack_tab5 = {
    .name = "m5stack-tab5",
    .vendor = "M5Stack",
    .source = "Espressif esp-bsp m5stack_tab5 @ f259a6ab; orcsdr-tab5 sdkconfig.defaults (C6); not run by this firmware",
    .unsupported = "No display backend for ILI9881C/ST7123 DSI panels or GT911/ST7123 touch; USB host power and "
                   "Wi-Fi enable go through I2C expanders the console does not drive yet; RTC, camera, IMU and "
                   "battery gauge unused. The Tab5 keeps running its own firmware (apps/orcsdr-tab5).",
    .status = ORCSDR_PROFILE_DOCUMENTED,
    .console_supported = false,

    .i2c_port = 1, .i2c_sda = 31, .i2c_scl = 32,
    .audio_mclk = 30, .audio_bclk = 27, .audio_ws = 29, .audio_dout = 26, .audio_din = 28, .audio_pa_enable = ORCSDR_PIN_NONE,
    .has_sd = true, .sd_power_enable = ORCSDR_PIN_NONE, .sd_power_active_low = false,

    .has_hosted_c6 = true, .c6_reset = 15,
    .c6_sdio_clk = 12, .c6_sdio_cmd = 13, .c6_sdio_d0 = 11, .c6_sdio_d1 = 10, .c6_sdio_d2 = 9, .c6_sdio_d3 = 8,

    .has_ethernet = false, .eth_phy = NULL,
    .eth_phy_addr = ORCSDR_PIN_NONE, .eth_phy_reset = ORCSDR_PIN_NONE, .eth_mdc = ORCSDR_PIN_NONE, .eth_mdio = ORCSDR_PIN_NONE,
    .eth_rmii_clk_in = ORCSDR_PIN_NONE, .eth_tx_en = ORCSDR_PIN_NONE, .eth_txd0 = ORCSDR_PIN_NONE, .eth_txd1 = ORCSDR_PIN_NONE,
    .eth_crs_dv = ORCSDR_PIN_NONE, .eth_rxd0 = ORCSDR_PIN_NONE, .eth_rxd1 = ORCSDR_PIN_NONE,

    .has_usb_host_typea = true, .usb_vbus_gpio = ORCSDR_PIN_NONE, .usb_vbus_ioexp = {1, 3},

    .ioexp_addr = {0x43, 0x44},
    .ioexp_wifi_en = {1, 0}, .ioexp_lcd_en = {0, 4},
    .ioexp_touch_en = {0, 5}, .ioexp_speaker_en = {0, 1},
};
