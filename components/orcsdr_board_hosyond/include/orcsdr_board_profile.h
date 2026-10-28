#pragma once
/*
 * Board profile: the facts about an ESP32-P4 board that the application would otherwise hard-code
 * (pins and which peripherals exist). One profile is selected at build time; code asks the profile
 * instead of using GPIO numbers. A peripheral the board lacks is marked absent (pin -1 / false) and
 * the matching feature is hidden or reports "unavailable" instead of failing.
 *
 * Display panels are described separately (orcsdr_display_profile.h): the same board can drive
 * different panels, and one panel can sit on different boards.
 */
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;           /* "waveshare-esp32-p4-module-dev-kit" */

    /* I2C bus routed to the DSI connector (panel controller, touch) and shared with the audio codec. */
    int i2c_port, i2c_sda, i2c_scl;

    /* Speaker path: I2S pins, codec on the shared I2C bus, amplifier enable. -1 = absent. */
    int audio_mclk, audio_bclk, audio_ws, audio_dout, audio_din, audio_pa_enable;

    /* microSD on SDMMC; power switch pin and its active level. -1 = no switch / no slot. */
    bool has_sd;
    int sd_power_enable;
    bool sd_power_active_low;

    /* Wi-Fi coprocessor (ESP32-C6 over ESP-Hosted SDIO). Other boards may use SPI or have none. */
    bool has_hosted_c6;
    int c6_reset;
    bool has_ethernet;          /* a wired port is present (not used by the application yet) */
} orcsdr_board_profile_t;

/* The profile this firmware was built for. */
const orcsdr_board_profile_t *orcsdr_board_profile(void);

#ifdef __cplusplus
}
#endif
