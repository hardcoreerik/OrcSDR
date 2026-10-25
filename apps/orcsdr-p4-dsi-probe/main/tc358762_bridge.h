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

#define TC358762_LCDCTRL_UNK6         (1u << 6)
#define TC358762_LCDCTRL_VTGEN        (1u << 4)
#define TC358762_LCDCTRL_RGB888       (1u << 8)
#define TC358762_LCDCTRL_VSDELAY(v)   (((v) & 0xfffu) << 20)

#define TC358762_SYSCTRL_VALUE        0x040f

/* The bridge's DE window starts (HSW + HBP) after hsync, but the D4 stream from
 * the P4 delivers active pixels earlier than that, so content sits displaced to
 * the right with the line tail wrapped onto the left edge. Measured on the
 * panel with a full-width digit ruler: 140 px of displacement at a compensation
 * of 100. Compensation is transferred out of the front porch rather than added,
 * so the bridge line total stays 1058 and matches the host. */
#define TC358762_DE_START_COMP        210

/* Upstream tc358762.c: VSDELAY(1) | RGB888 | UNK6 | VTGEN = 0x00100150 */
#define TC358762_LCDCTRL_UPSTREAM     (TC358762_LCDCTRL_VSDELAY(1) | \
                                       TC358762_LCDCTRL_RGB888 | \
                                       TC358762_LCDCTRL_UNK6 | \
                                       TC358762_LCDCTRL_VTGEN)

/* Same as above with VTGEN cleared, so the LCD raster is slaved to the sync
 * pulses arriving over DSI instead of free-running from the bridge's own
 * timing generator. Used because a free-running generator is clocked from a
 * different PLL than the P4 pixel clock, which shows up as the image sitting
 * at a constant horizontal offset and slowly cycling. */
#define TC358762_LCDCTRL_DSI_SYNCED   (TC358762_LCDCTRL_VSDELAY(1) | \
                                       TC358762_LCDCTRL_RGB888 | \
                                       TC358762_LCDCTRL_UNK6)

