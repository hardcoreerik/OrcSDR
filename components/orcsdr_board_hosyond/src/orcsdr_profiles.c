#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "sdkconfig.h"
#include "orcsdr_board_profile.h"
#include "orcsdr_display_profile.h"
#include "boards/board_list.h"

static const char *TAG = "PROFILES";

/* Every board is compiled in so a mistake in any profile file is caught by any build; Kconfig ORCSDR_BOARD
 * chooses which one this firmware uses. */
static const orcsdr_board_profile_t *const k_boards[] = {
    &g_board_waveshare_p4_module_dev_kit,
    &g_board_waveshare_p4_wifi6_poe_eth,
    &g_board_waveshare_p4_wifi6_db,
    &g_board_waveshare_p4_eth,
    &g_board_m5stack_tab5,
};

bool orcsdr_board_c6_image_applies(void)
{
    const orcsdr_board_profile_t *b = orcsdr_board_profile();
    return b->has_hosted_c6 && b->wifi_coprocessor && strcmp(b->wifi_coprocessor, "ESP32-C6") == 0;
}

const orcsdr_board_profile_t *orcsdr_board_profile(void)
{
#if defined(CONFIG_ORCSDR_BOARD_M5STACK_TAB5)
    return &g_board_m5stack_tab5;
#elif defined(CONFIG_ORCSDR_BOARD_WAVESHARE_P4_ETH)
    return &g_board_waveshare_p4_eth;
#elif defined(CONFIG_ORCSDR_BOARD_WAVESHARE_P4_WIFI6_DB)
    return &g_board_waveshare_p4_wifi6_db;
#elif defined(CONFIG_ORCSDR_BOARD_WAVESHARE_P4_WIFI6_POE_ETH)
    return &g_board_waveshare_p4_wifi6_poe_eth;
#else
    return &g_board_waveshare_p4_module_dev_kit;
#endif
}

size_t orcsdr_board_profiles(const orcsdr_board_profile_t *const **list)
{
    if (list) *list = k_boards;
    return sizeof(k_boards) / sizeof(k_boards[0]);
}

const char *orcsdr_profile_status_name(orcsdr_profile_status_t status)
{
    switch (status) {
    case ORCSDR_PROFILE_BENCH_VERIFIED: return "bench-verified";
    case ORCSDR_PROFILE_DOCUMENTED: return "documented-only";
    default: return "skeleton";
    }
}

void orcsdr_board_profile_log(void)
{
    const orcsdr_board_profile_t *b = orcsdr_board_profile();
    ESP_LOGI(TAG, "board: %s (%s), profile %s, console %s", b->name, b->vendor, orcsdr_profile_status_name(b->status),
             b->console_supported ? "supported" : "NOT supported");
    if (b->status != ORCSDR_PROFILE_BENCH_VERIFIED) {
        ESP_LOGW(TAG, "this board's pins are not bench-verified with this firmware; source: %s", b->source);
    }
    if (b->unsupported) ESP_LOGW(TAG, "unsupported here: %s", b->unsupported);
    if (b->has_hosted_c6 && !orcsdr_board_c6_image_applies()) {
        ESP_LOGW(TAG, "Wi-Fi coprocessor is %s: the embedded coprocessor-update image is for the ESP32-C6 and is never offered to it; "
                 "Wi-Fi works only if the chip already runs a compatible ESP-Hosted slave; coprocessor update is disabled",
                 b->wifi_coprocessor ? b->wifi_coprocessor : "unknown");
    }
    {
        /* The ESP-Hosted host rejects a link whose chip id differs from the build's coprocessor target, which looks like a
         * dead SDIO bus ("hosted_connect" failure). Say so at boot. */
#if defined(CONFIG_ESP_HOSTED_CP_TARGET_ESP32C5)
        const char *target = "ESP32-C5";
#elif defined(CONFIG_ESP_HOSTED_CP_TARGET_ESP32C6)
        const char *target = "ESP32-C6";
#else
        const char *target = NULL;
#endif
        if (b->has_hosted_c6 && b->wifi_coprocessor && target && strcmp(b->wifi_coprocessor, target) != 0) {
            ESP_LOGE(TAG, "build targets a %s coprocessor but profile %s has an %s: the host will reject the link; set "
                     "CONFIG_SLAVE_IDF_TARGET to match (see the board overlay)", target, b->name, b->wifi_coprocessor);
        }
    }
#if defined(CONFIG_ESP_HOSTED_HOST_SDIO_PIN_CLK) && defined(CONFIG_ESP_HOSTED_HOST_RESET_GPIO)
    /* The ESP-Hosted pins live in sdkconfig; a profile that disagrees means the wrong board was selected. */
    if (b->has_hosted_c6 &&
        (b->c6_sdio_clk != CONFIG_ESP_HOSTED_HOST_SDIO_PIN_CLK || b->c6_sdio_cmd != CONFIG_ESP_HOSTED_HOST_SDIO_PIN_CMD ||
         b->c6_sdio_d0 != CONFIG_ESP_HOSTED_HOST_SDIO_PIN_D0 || b->c6_sdio_d1 != CONFIG_ESP_HOSTED_HOST_SDIO_PIN_D1 ||
         b->c6_sdio_d2 != CONFIG_ESP_HOSTED_HOST_SDIO_PIN_D2 || b->c6_sdio_d3 != CONFIG_ESP_HOSTED_HOST_SDIO_PIN_D3 ||
         (b->c6_reset >= 0 && b->c6_reset != CONFIG_ESP_HOSTED_HOST_RESET_GPIO))) {
        ESP_LOGE(TAG, "ESP-Hosted SDIO pins in sdkconfig do not match profile %s: wrong board selected? Wi-Fi will not start", b->name);
    }
#endif
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
