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

/* Upstream tc358762.c: VSDELAY(1) | RGB888 | UNK6 | VTGEN = 0x00100150 */
#define TC358762_LCDCTRL_UPSTREAM     (TC358762_LCDCTRL_VSDELAY(1) | \
                                       TC358762_LCDCTRL_RGB888 | \
                                       TC358762_LCDCTRL_UNK6 | \
                                       TC358762_LCDCTRL_VTGEN)
