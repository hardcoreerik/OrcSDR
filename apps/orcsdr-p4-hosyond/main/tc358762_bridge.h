#pragma once

/*
 * TC358762 DSI-to-RGB bridge on a Raspberry Pi 7"-compatible panel.
 *
 * Register facts taken from the mainline kernel driver
 * drivers/gpu/drm/bridge/tc358762.c. The bridge is programmed with MIPI DSI
 * generic long writes (data type 0x29): a 16-bit register address followed by a
 * 32-bit value, both little-endian. Sending DCS writes instead does not reach
 * the bridge's register interface.
 *
 * Until this runs, the panel is powered and backlit but displays nothing: the
 * bridge is out of reset yet not converting DSI video into parallel RGB.
 */

/* PPI layer */
#define TC358762_PPI_STARTPPI        0x0104
#define TC358762_PPI_LPTXTIMECNT     0x0114
#define TC358762_PPI_D0S_ATMR        0x0144
#define TC358762_PPI_D1S_ATMR        0x0148
#define TC358762_PPI_D0S_CLRSIPO     0x0164
#define TC358762_PPI_D1S_CLRSIPO     0x0168

/* DSI layer */
#define TC358762_DSI_STARTDSI        0x0204
#define TC358762_DSI_LANEENABLE      0x0210

/* Video path */
#define TC358762_LCDCTRL             0x0420
#define TC358762_LCD_HS_HBP          0x0424  /* [31:16] HBP, [15:0] HSW */
#define TC358762_LCD_HDISP_HFP       0x0428  /* [31:16] HFP, [15:0] H display */
#define TC358762_LCD_VS_VBP          0x042c  /* [31:16] VBP, [15:0] VSW */
#define TC358762_LCD_VDISP_VFP       0x0430  /* [31:16] VFP, [15:0] V display */
#define TC358762_SPICMR              0x0450

/* System */
#define TC358762_SYSCTRL             0x0464

/* Values */
#define TC358762_LANEENABLE_L0EN_CLEN 0x03
#define TC358762_LPX_PERIOD           3
#define TC358762_CLRSIPO_COUNT        5

/* LCDCTRL fields, corrected per Tomi Valkeinen's upstream tc358762-fixes
 * series (Oct 2026). The defines inherited from raspberrypi/linux rpi-6.6.y
 * were "based on guesswork that this matches TC358764" and are wrong: what that
 * tree calls LCDCTRL_UNK6|LCDCTRL_VTGEN are the two high bits of PXLFORM, and
 * what it calls VTGEN (bit 4) is PXLFORM's low bit.
 * https://lore.kernel.org/dri-devel/20261015-tc358762-fixes-v5-3-7cd721a6451b@ideasonboard.com/
 * Cross-checked against the datasheet-derived F&S Elektronik driver. */
#define TC358762_LCDCTRL_MSF          (1u << 0)   /* magic square FRC, RGB666 */
#define TC358762_LCDCTRL_VTGEN        (1u << 1)   /* use chip clock for timing */
#define TC358762_LCDCTRL_PXLFORM_RGB888 (5u << 4) /* PXLFORM 6:4, 5 = R:G:B 8:8:8 */
#define TC358762_LCDCTRL_PXLFORM_MASK (7u << 4)
#define TC358762_LCDCTRL_DPI_EN       (1u << 8)   /* DPI output enable */
#define TC358762_LCDCTRL_HSYNC_POL    (1u << 17)
#define TC358762_LCDCTRL_DE_POL       (1u << 18)
#define TC358762_LCDCTRL_VSYNC_POL    (1u << 19)
#define TC358762_LCDCTRL_DCLK_POL     (1u << 20)

#define TC358762_SYSCTRL_VALUE        0x040f

/* VFUEN: "video frame update enable", the latch that uploads the VTG timing
 * registers. Every driver that actually uses VTG writes it after programming
 * 0x0424/0x0428/0x042c/0x0430 and before enabling VTG. It is also bit 0 of
 * register 0x0434, which doubles as the vertical front porch field once VTG is
 * running, so it must be written before LCDCTRL turns the generator on. */
#define TC358762_VFUEN               0x0434
#define TC358762_VFUEN_ENABLE        0x01

/* DPI enabled, RGB888, pixel clock polarity inverted, timing generator ON.
 * 0x00100150 (the value upstream tc358762.c writes) is the same thing with
 * VTGEN clear: DCLK_POL | DPI_EN | PXLFORM_RGB888. */
#define TC358762_LCDCTRL_VTG_RGB888   (TC358762_LCDCTRL_DCLK_POL | \
                                       TC358762_LCDCTRL_DPI_EN | \
                                       TC358762_LCDCTRL_PXLFORM_RGB888 | \
                                       TC358762_LCDCTRL_VTGEN)

/* The value the upstream driver writes, kept so the log shows which mode is
 * active. Note it leaves VTGEN clear, i.e. the bridge duplicates the timings it
 * receives over DSI instead of generating its own. */
#define TC358762_LCDCTRL_DUP_DSI      (TC358762_LCDCTRL_DCLK_POL | \
                                       TC358762_LCDCTRL_DPI_EN | \
                                       TC358762_LCDCTRL_PXLFORM_RGB888)


