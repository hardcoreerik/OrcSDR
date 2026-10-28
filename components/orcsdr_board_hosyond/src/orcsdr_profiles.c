#include <stddef.h>
#include <stdio.h>
#include "esp_log.h"
#include "orcsdr_board_profile.h"
#include "orcsdr_display_profile.h"

static const char *TAG = "PROFILES";

/* Waveshare ESP32-P4-Module-DEV-KIT. Pin sources: the board schematic and Espressif's espp BSP;
 * the SD, audio and C6 pins have been exercised on the bench (docs/HOSYOND_7IN_DSI_P4.md). */
static const orcsdr_board_profile_t k_waveshare_p4_devkit = {
    .name = "waveshare-esp32-p4-module-dev-kit",
    .i2c_port = 1, .i2c_sda = 7, .i2c_scl = 8,
    .audio_mclk = 13, .audio_bclk = 12, .audio_ws = 10, .audio_dout = 9, .audio_din = 11, .audio_pa_enable = 53,
    .has_sd = true, .sd_power_enable = 45, .sd_power_active_low = true,
    .has_hosted_c6 = true, .c6_reset = 54,
    .has_ethernet = false,
};

const orcsdr_board_profile_t *orcsdr_board_profile(void)
{
    return &k_waveshare_p4_devkit;
}

/* Verified panels only. The Hosyond 7" carries the Raspberry-Pi-style ATTINY88 panel controller (0x45); its
 * FT5x06 touch controller (0x38) only answers after the panel power-up sequence, so it is not required here. */
static const orcsdr_display_profile_t k_displays[] = {
    {.name = "hosyond-7in-800x480-dsi", .width = 800, .height = 480, .verified_on_hardware = true,
     .required_i2c = {0x45, 0, 0, 0}},
};

const orcsdr_display_profile_t *orcsdr_display_detect(i2c_master_bus_handle_t bus, unsigned char *found)
{
    unsigned char seen[16] = {0};
    size_t n = 0;
    /* Only the addresses panels and their helpers are known to use: scanning all 112 would add seconds to boot. */
    static const unsigned char k_probe[] = {0x14, 0x15, 0x18, 0x2A, 0x38, 0x41, 0x45, 0x48, 0x5D};
    for (size_t p = 0; p < sizeof(k_probe) && n < 15; ++p) {
        const int addr = k_probe[p];
        /* The first transfer after power-up often fails (see the ATTINY notes), so try twice. */
        if (i2c_master_probe(bus, (uint16_t)addr, 20) == ESP_OK || i2c_master_probe(bus, (uint16_t)addr, 20) == ESP_OK) {
            seen[n++] = (unsigned char)addr;
        }
    }
    if (found) {
        for (size_t i = 0; i < sizeof(seen); ++i) found[i] = seen[i];
    }
    for (size_t d = 0; d < sizeof(k_displays) / sizeof(k_displays[0]); ++d) {
        bool all = true;
        for (int r = 0; r < 4 && k_displays[d].required_i2c[r]; ++r) {
            bool present = false;
            for (size_t i = 0; i < n; ++i) present |= seen[i] == k_displays[d].required_i2c[r];
            all &= present;
        }
        if (all) {
            ESP_LOGI(TAG, "display detected: %s (%dx%d)", k_displays[d].name, k_displays[d].width, k_displays[d].height);
            return &k_displays[d];
        }
    }
    return NULL;
}

void orcsdr_display_report(const unsigned char *found)
{
    char list[16 * 5 + 1] = "";
    size_t len = 0;
    for (int i = 0; found && i < 16 && found[i]; ++i) len += (size_t)snprintf(list + len, sizeof(list) - len, "0x%02X ", found[i]);
    ESP_LOGW(TAG, "no known display on the DSI I2C bus; devices answering: %s", len ? list : "none");
    ESP_LOGW(TAG, "send this line to add a profile: board=%s", orcsdr_board_profile()->name);
}
