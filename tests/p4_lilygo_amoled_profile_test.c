/* Host check for the LilyGO T-Display-P4 AMOLED profile. No ESP-IDF.
 *   gcc -Wall -Werror -I components/orcsdr_board_hosyond/include -I components/orcsdr_board_hosyond/src \
 *       tests/p4_lilygo_amoled_profile_test.c -o /tmp/p4_lilygo_profile && /tmp/p4_lilygo_profile
 */
#include <stdio.h>
#include <string.h>
#include "boards/board_lilygo_t_display_p4_amoled.c"

static int g_fails;

static void expect(int cond, const char *what)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", what);
        g_fails++;
    }
}

int main(void)
{
    const orcsdr_board_profile_t *b = &g_board_lilygo_t_display_p4_amoled;
    expect(strcmp(b->name, "lilygo-t-display-p4-amoled") == 0, "name");
    expect(b->status == ORCSDR_PROFILE_DOCUMENTED, "status documented");
    expect(!b->console_supported, "console not supported yet");
    expect(b->i2c_sda == 7 && b->i2c_scl == 8, "panel I2C");
    expect(b->codec_i2c_sda == 20 && b->codec_i2c_scl == 21 && b->codec_i2c_port == 0, "codec I2C");
    expect(b->audio_ws == 9 && b->audio_dout == 10 && b->audio_din == 11, "I2S swapped vs Waveshare");
    expect(b->audio_mclk == 13 && b->audio_bclk == 12, "I2S clocks");
    expect(b->audio_pa_enable == ORCSDR_PIN_NONE, "no PA gpio");
    expect(b->ioexp_speaker_en.dev == 0 && b->ioexp_speaker_en.pin == 6, "speaker enable");
    expect(b->has_hosted_c6 && strcmp(b->wifi_coprocessor, "ESP32-C6") == 0, "C6");
    expect(b->c6_reset == ORCSDR_PIN_NONE, "C6 enable is not a gpio");
    expect(b->ioexp_wifi_en.dev == 0 && b->ioexp_wifi_en.pin == 14, "C6 enable expander");
    expect(b->c6_sdio_clk == 18 && b->c6_sdio_cmd == 19 && b->c6_sdio_d0 == 14 && b->c6_sdio_d3 == 17, "SDIO");
    expect(b->has_radio && strcmp(b->radio_part, "SX1262") == 0, "sx1262");
    expect(b->radio_sclk == 2 && b->radio_mosi == 3 && b->radio_miso == 4, "radio spi");
    expect(b->radio_cs == 24 && b->radio_busy == 6 && b->radio_spi_hz == 10000000, "radio cs busy hz");
    expect(b->radio_rst.pin == 16 && b->radio_dio1.pin == 17 && b->radio_ant_sw.pin == 1, "radio expander");
    expect(b->ioexp_addr[0] == 0x20 && b->ioexp_addr[1] == 0, "xl9535");
    expect(b->ioexp_screen_rst.pin == 2 && b->ioexp_touch_rst.pin == 3, "panel resets");
    expect(b->ioexp_sd_power.pin == 15 && b->ioexp_usb_phy_en.pin == 10 && b->ioexp_eth_rst.pin == 5, "power lines");
    expect(b->sd_power_enable == ORCSDR_PIN_NONE && b->has_sd, "sd slot without gpio power");
    expect(b->has_ethernet && b->eth_phy_reset == ORCSDR_PIN_NONE && b->eth_mdc == 31, "ethernet");
    expect(!b->has_usb_host_typea && b->usb_vbus_gpio == ORCSDR_PIN_NONE, "usb host not claimed");
    if (g_fails) {
        fprintf(stderr, "%d checks failed\n", g_fails);
        return 1;
    }
    puts("lilygo amoled profile ok");
    return 0;
}
