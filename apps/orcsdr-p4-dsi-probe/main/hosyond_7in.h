#pragma once

/*
 * Hosyond 7" 800x480 MIPI-DSI panel on a Waveshare ESP32-P4-Module-DEV-KIT.
 *
 * Profile 0 is the upstream Linux Raspberry Pi 7" touchscreen mode and is the
 * baseline. The other profiles are kept for later controlled comparison only.
 *
 * Our unit answers on I2C 0x45 with revision 0xC3, the value the mainline
 * rpi-panel-attiny-regulator driver accepts, and its PORTA/PORTB readbacks
 * match what we write, so it is treated as a Raspberry Pi 7"-compatible
 * TC358762 + ATTINY88 module.
 *
 * Known-good reference recovered read-only from the Raspberry Pi 4B boot
 * partition that drove this panel successfully:
 *
 *   config.txt : display_auto_detect=1, dtoverlay=vc4-kms-v3d,
 *                disable_fw_kms_setup=1  -> the panel was brought up by an
 *                auto-detected DSI overlay, not a hand-written config.
 *   overlays/vc4-kms-dsi-7inch.dtbo
 *                bridge@0 = "toshiba,tc358762"
 *                panel@1  = "raspberrypi,7inch-dsi" (simple-panel)
 *                reg_display@0x45 = "raspberrypi,7inch-touchscreen-panel-regulator"
 *                ts@0x38          = "edt,edt-ft5506"
 *                This is exactly our topology, which is why the ATTINY id 0xC3
 *                is the signature that matters.
 *   overlays/vc4-kms-dsi-waveshare-800x480.dtbo
 *                "panel-dsi-generic", 800x480, 1 lane, RGB888, MODE_VIDEO,
 *                clock 27.777 MHz, HFP 59 / HSW 2 / HBP 45,
 *                VFP 7 / VSW 2 / VBP 22   -> 906x511 = 60.00 Hz.
 *
 * The two 800x480 references agree on HSW/HBP/VFP/VSW and on VBP 21..22, and
 * differ only in HFP and pixel clock. Neither is used as the active profile
 * yet; profile 0 stays the upstream Pi 7" modeline for a single controlled
 * test.
 *
 * The device seen at I2C 0x18 on this bus is not part of the panel (it is the
 * board's ES8311 codec); the panel itself only contributes 0x38 and 0x45.
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

#define H4_PROFILE_COUNT 5
#define H4_DEFAULT_PROFILE 4

static const h4_profile_t h4_profiles[H4_PROFILE_COUNT] = {
    {
        /* drivers/gpu/drm/panel/panel-raspberrypi-touchscreen.c: the modeline
         * comes from the Raspberry Pi firmware with HFP=1 plugged in. 849x510
         * total at 25.9794 MHz is 60.0 Hz. RGB888 over one lane needs
         * 25.9794 * 24 = 623.5 Mbps. */
        .name = "upstream-rpi-panel", .hfp = 1, .hsw = 2, .hbp = 46,
        .vfp = 7, .vsw = 2, .vbp = 21, .dpi_mhz = 25.9794f, .lane_mbps = 600.0f,
    },
    {
        /* Reported modeline "800 931 933 978 / 480 487 489 511" at 30 MHz:
         * HFP=931-800=131, HSW=933-931=2, HBP=978-933=45,
         * VFP=487-480=7, VSW=489-487=2, VBP=511-489=22. */
        .name = "modeline-30mhz", .hfp = 131, .hsw = 2, .hbp = 45,
        .vfp = 7, .vsw = 2, .vbp = 22, .dpi_mhz = 30.0f, .lane_mbps = 720.0f,
    },
    {
        /* Community ICN6211 report for a Hosyond 7" 800x480 module. Our unit
         * does not contain an ICN6211, so this is comparison material only. */
        .name = "community-icn6211", .hfp = 40, .hsw = 48, .hbp = 90,
        .vfp = 7, .vsw = 1, .vbp = 23, .dpi_mhz = 27.5f, .lane_mbps = 660.0f,
    },
    {
        /* Default, and the only profile that satisfies the panel's documented
         * constraints: hsw + hbp must equal 46 and vsw + vbp must equal 23.
         * https://lore.kernel.org/dri-devel/20261015-tc358762-fixes-v5-13-7cd721a6451b@ideasonboard.com/
         * An off-by-one in vsw + vbp is documented as producing a visible
         * image shift, and the previous profile had both sums wrong
         * (48 and 24). Values match a working ESP32-P4 + Raspberry Pi 7"
         * configuration. The host DPI stream and the bridge VTG registers are
         * given identical values so the payload and the DE window start at the
         * same pixel: 977 x 510 at 25.98 MHz = 52.1 Hz. */
        .name = "datasheet-800x480", .hfp = 131, .hsw = 2, .hbp = 44,
        .vfp = 7, .vsw = 2, .vbp = 21, .dpi_mhz = 25.98f, .lane_mbps = 600.0f,
    },
    {
        /* Horizontal alignment, derived from two measured points rather than
         * chosen. Driving this profile's HBP swept the whole DE window on both
         * the host stream and the bridge VTG, and the panel displacement was
         * measured from the ruler card as:
         *
         *     hbp 146 -> 140 px displacement
         *     hbp 256 ->  40 px displacement
         *
         * The two points give offset(h) = 140 - 0.9091 * (h - 146), which is a
         * one-to-one response with 0.909 px of image movement per pixel of HBP,
         * so the zero crossing is at h = 300. Horizontal total becomes
         * 800 + 2 + 300 + 131 = 1233, i.e. 25.98 MHz / (1233 * 510) = 41.3 Hz,
         * which the panel accepts; vertical timing is untouched because the
         * vertical geometry was already measured correct. The datasheet profile
         * above is kept unchanged as the recovery configuration. */
        .name = "measured-hbp300", .hfp = 131, .hsw = 2, .hbp = 300,
        .vfp = 7, .vsw = 2, .vbp = 21, .dpi_mhz = 25.98f, .lane_mbps = 600.0f,
    },
};
