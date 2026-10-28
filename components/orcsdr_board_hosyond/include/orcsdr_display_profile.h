#pragma once
/*
 * Display profile: what a DSI panel needs (size, controllers to recognise it by) and how to find it.
 *
 * DSI has no EDID. The ribbon does carry an I2C bus, so a panel is recognised by probing that bus
 * for the controllers that panel is known to have. A profile is only listed once it has been
 * verified on real hardware. When nothing matches, the firmware must not guess timings: it reports
 * what it found (orcsdr_display_report) and the board runs without a display.
 */
#include <stdbool.h>
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;           /* "hosyond-7in-800x480-dsi" */
    int width, height;          /* native size in the orientation the firmware drives */
    bool verified_on_hardware;
    /* 7-bit I2C addresses that must all answer for this panel; 0 terminates. */
    unsigned char required_i2c[4];
} orcsdr_display_profile_t;

/* Probe the DSI connector's I2C bus and return the matching known profile, or NULL when nothing
 * known answered. Fills `found` (if not NULL) with every address that responded (0 terminated,
 * up to 16 entries) so an unknown panel can be reported. */
const orcsdr_display_profile_t *orcsdr_display_detect(i2c_master_bus_handle_t bus, unsigned char *found);

/* Log what the probe found. Call when orcsdr_display_detect() returned NULL. */
void orcsdr_display_report(const unsigned char *found);

#ifdef __cplusplus
}
#endif
