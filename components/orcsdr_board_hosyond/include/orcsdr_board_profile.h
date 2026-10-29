#pragma once
/*
 * Board profile: the facts about an ESP32-P4 board that the application would otherwise hard-code
 * (pins and which peripherals exist). One profile is selected at build time (Kconfig ORCSDR_BOARD);
 * code asks the profile instead of using GPIO numbers. A peripheral the board lacks is marked absent
 * (pin -1 / false / line.dev -1) and the matching feature is hidden or reports "unavailable" instead
 * of failing.
 *
 * Every profile file lists the source of each value next to it and carries a status:
 *   BENCH_VERIFIED  exercised on the real board with this firmware;
 *   DOCUMENTED      taken from a vendor schematic, vendor BSP or Espressif BSP, never run on hardware here;
 *   SKELETON        identity only, pins not yet established.
 * "Documented" is not "measured": a documented profile must be bench-checked before it is relied on.
 *
 * Display panels are described separately (orcsdr_display_profile.h): the same board can drive
 * different panels, and one panel can sit on different boards.
 */
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ORCSDR_PIN_NONE (-1)

typedef enum {
    ORCSDR_PROFILE_SKELETON = 0,
    ORCSDR_PROFILE_DOCUMENTED,
    ORCSDR_PROFILE_BENCH_VERIFIED,
} orcsdr_profile_status_t;

/* A control line on an I2C GPIO expander: which expander (index into ioexp_addr) and which pin. dev -1 = absent. */
typedef struct {
    signed char dev;
    signed char pin;
} orcsdr_ioexp_line_t;

#define ORCSDR_IOEXP_NONE ((orcsdr_ioexp_line_t){-1, -1})

typedef struct {
    const char *name;           /* "waveshare-esp32-p4-module-dev-kit" */
    const char *vendor;         /* "Waveshare" */
    const char *source;         /* where the values come from: schematic / BSP / bench */
    const char *unsupported;    /* what OrcSDR cannot do on this board yet, or NULL */
    orcsdr_profile_status_t status;
    /* True when this firmware can drive the board's screen and touch. False = headless use only
     * (or a display driver is still missing), whatever the other fields say. */
    bool console_supported;

    /* I2C bus routed to the DSI connector (panel controller, touch) and shared with the audio codec. */
    int i2c_port, i2c_sda, i2c_scl;

    /* Speaker path: I2S pins, codec on the shared I2C bus, amplifier enable. -1 = absent. */
    int audio_mclk, audio_bclk, audio_ws, audio_dout, audio_din, audio_pa_enable;

    /* microSD on SDMMC. sd_power_enable is a power switch pin (-1 = none: the slot is powered
     * unconditionally or by an on-chip LDO, which is not the same as having no slot). */
    bool has_sd;
    int sd_power_enable;
    bool sd_power_active_low;

    /* Wi-Fi coprocessor (ESP32-C6 over ESP-Hosted SDIO). Other boards may use SPI or have none.
     * On boards where the C6 power or reset is on an expander, c6_reset is -1 and ioexp_wifi_en is set. */
    bool has_hosted_c6;
    int c6_reset;
    int c6_sdio_clk, c6_sdio_cmd, c6_sdio_d0, c6_sdio_d1, c6_sdio_d2, c6_sdio_d3;

    /* Wired Ethernet: RMII PHY name (NULL = none). Not used by the application yet. */
    bool has_ethernet;
    const char *eth_phy;

    /* USB host (RTL-SDR dongle). VBUS switching, when the board has it, is a GPIO or an expander line. */
    bool has_usb_host_typea;
    int usb_vbus_gpio;
    orcsdr_ioexp_line_t usb_vbus_ioexp;

    /* I2C GPIO expanders on the main I2C bus (7-bit addresses, 0 = none) and the lines this board puts on them. */
    unsigned char ioexp_addr[2];
    orcsdr_ioexp_line_t ioexp_wifi_en, ioexp_lcd_en, ioexp_touch_en, ioexp_speaker_en;
} orcsdr_board_profile_t;

/* The profile this firmware was built for (Kconfig ORCSDR_BOARD). */
const orcsdr_board_profile_t *orcsdr_board_profile(void);

/* Every profile compiled into this firmware, so tools and docs can list them. Returns the count. */
size_t orcsdr_board_profiles(const orcsdr_board_profile_t *const **list);

/* Name of a status for logs. */
const char *orcsdr_profile_status_name(orcsdr_profile_status_t status);

/* Log the selected profile and warn when it disagrees with the ESP-Hosted pins the build was
 * configured with (sdkconfig), which would mean the wrong board was selected. */
void orcsdr_board_profile_log(void);

#ifdef __cplusplus
}
#endif
