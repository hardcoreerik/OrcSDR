#pragma once

/*
 * Hosyond 7" 800x480 MIPI-DSI panel on a Waveshare ESP32-P4-Module-DEV-KIT.
 *
 * PROVENANCE - none of these electrical values is validated on our unit yet.
 * The probe sweeps every candidate and logs which one is active, so a result is
 * never attributed to the wrong timing set.
 *
 * 1. Community report for a Hosyond 7" 800x480 DSI module: ICN6211 bridge,
 *    single-lane DSI, ~660 Mbps, horizontal 48/90/40, vertical 1/7/23.
 * 2. Raspberry Pi 7" touch display kernel timings, as reported for a related
 *    Hosyond part: "800x480: 60 30000 800 931 933 978 480 487 489 511".
 * 3. Raspberry Pi 7" panel values reported as observed-stable on ESP32-P4 by
 *    the ESP32-P4-DSI-Support-Hub project (CC BY-NC-ND 4.0 - facts only).
 *
 * Our unit answers on I2C 0x45 with revision 0xC3, which identifies it as a
 * Raspberry Pi 7"-compatible module, so profiles B and C are the stronger
 * candidates and profile A is kept as the originally-briefed hypothesis.
 */

/* ---- Board facts (Waveshare ESP32-P4-Module-DEV-KIT) ---- */
#define PROBE_I2C_PORT      1   /* I2C_NUM_1, routed to the DSI connector */
#define PROBE_I2C_SDA_GPIO  7
#define PROBE_I2C_SCL_GPIO  8
#define PROBE_I2C_FREQ_HZ   100000

#define PROBE_DSI_PHY_LDO_CHAN        3   /* LDO_VO3 -> VDD_MIPI_DPHY */
#define PROBE_DSI_PHY_LDO_VOLTAGE_MV  2500

#define HOSYOND_H_RES 800
#define HOSYOND_V_RES 480

typedef struct {
    const char *name;
    int hfp, hsw, hbp;
    int vfp, vsw, vbp;
    float dpi_mhz;
    float lane_mbps;
} h4_profile_t;

#define H4_PROFILE_COUNT 3

static const h4_profile_t h4_profiles[H4_PROFILE_COUNT] = {
    {
        .name = "A:community-icn6211", .hfp = 40, .hsw = 48, .hbp = 90,
        .vfp = 7, .vsw = 1, .vbp = 23, .dpi_mhz = 27.5f, .lane_mbps = 660.0f,
    },
    {
        .name = "B:rpi-kernel-60hz", .hfp = 131, .hsw = 2, .hbp = 45,
        .vfp = 7, .vsw = 2, .vbp = 29, .dpi_mhz = 30.0f, .lane_mbps = 720.0f,
    },
    {
        .name = "C:rpi-observed-stable", .hfp = 210, .hsw = 2, .hbp = 46,
        .vfp = 22, .vsw = 20, .vbp = 4, .dpi_mhz = 25.98f, .lane_mbps = 624.0f,
    },
};
