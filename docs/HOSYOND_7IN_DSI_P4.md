# Hosyond 7" DSI panel on the ESP32-P4: timings, pins, start-up, touch

State as of 2026-10-25, branch `codex/p4-hosyond-dsi-bringup`. Sources: the code in
`apps/orcsdr-p4-dsi-probe` and `apps/orcsdr-p4-hosyond`, serial logs from this bench,
and the Linux kernel drivers listed at the end (facts only). **Measured** means observed
on the bench; **inferred** means derived from code or references; **untested** means
nobody has run it.

## 1. Hardware

| Item | Value | Basis |
|---|---|---|
| Board | Waveshare ESP32-P4-Module-DEV-KIT, P4 rev v1.3, 16 MB flash, 32 MB PSRAM | measured (boot log) |
| Panel | Raspberry Pi 7"-class: TC358762 DSI-to-DPI bridge behind an ATTINY88 regulator/LED controller | measured: ATTINY ID 0xC3 (= v2) |
| Not an | ICN6211 | measured |
| Resolution | 800x480, RGB888 | measured |
| DSI link | 1 data lane, 600 Mbps, continuous HS clock | measured |
| Toolchain | ESP-IDF 6.1 (the clock-lane/PHY options do not exist in 5.5.x) | build error on older IDF |

## 2. Pins and addresses

| Signal | Where | Notes |
|---|---|---|
| I2C SDA / SCL | GPIO 7 / GPIO 8, I2C port 1, 100 kHz | routed to the DSI connector; internal pull-ups enabled |
| ATTINY88 | I2C 0x45 | ID register 0x80 reads 0xC3 |
| FT5x06 touch | I2C 0x38 | polled; no interrupt line is used |
| ES8311 audio codec | I2C 0x18 | on the board, **not** part of the panel |
| MIPI DPHY supply | LDO channel 3 at 2500 mV | `esp_ldo_regulator` |
| DSI lane pins | the P4's dedicated MIPI pads | fixed in silicon, not selectable |
| Console | UART0 via CH343 = COM8 | node JSON and logs share it |

ATTINY88 PORTC bits: 0 LED_EN, 1 touch reset (RST_TP_N), 2 LCD reset (RST_LCD_N),
3 bridge reset (RST_BRIDGE_N). PORTA bit 2 = LCD scan left-to-right. PORTB bit 7 =
main regulator. ATTINY reads need a separate address write, a ~6 ms gap, then the data
read; the first transfer after power-up often fails, so retry once.

## 3. Timings (measured working configuration)

Profile `rpi-observed-stable`:

| | front porch | sync | back porch | active | total |
|---|---|---|---|---|---|
| Horizontal (host) | 254 | 2 | 46 | 800 | 1102 |
| Vertical | 22 | 20 | 4 | 480 | 526 |

- DPI clock 25.98 MHz, so 1102 x 526 gives 44.8 Hz.
- Bridge side: HBP = 46 + 254 = 300, HFP = 0, same vertical values. Bridge line total
  is 1102, equal to the host's.
- RGB888 on one 600 Mbps lane is about 79% utilised; do not lower the lane rate or the
  pixel clock to move the image.

### Invariants (violating either blanks the panel)
1. The bridge's raster line total must equal the host DPI line total. The code logs
   both at start-up and prints an error if they differ.
2. The bridge DE compensation (`TC358762_DE_START_COMP`, 254) is **traded out of** the
   front porch, never added: `bridge_hbp = hbp + comp`, `bridge_hfp = hfp - comp`.
   So the profile's HFP must stay >= the compensation.

### Bridge registers (written as DSI generic long writes, DT 0x29)
`0x0210 LANEENABLE=3`, `0x0164/0x0168 CLRSIPO=5`, `0x0144/0x0148 ATMR=0`,
`0x0114 LPTXTIMECNT=3`, `0x0450 SPICMR=0`, `0x0420 LCDCTRL=0x00100150`,
`0x0464 SYSCTRL=0x040F`, `0x0424=0x012C0002`, `0x0428=0x00000320`,
`0x042C=0x00040014`, `0x0430=0x001601E0`, then after 100 ms `0x0104 STARTPPI=1`,
`0x0204 STARTDSI=1`, 100 ms.

**Open question:** upstream notes say LCDCTRL bit 1 is VTGEN, which 0x00100150 leaves
clear, so the bridge timing registers may be inert. An in-session sweep (VFUEN +
VTGEN on, bridge HBP 300/200/100) looked centred in every state, which fits "inert",
but boot-to-boot variation was present at that time, so it is not a clean result.
Bridge registers cannot be read back (every read returns no response).

## 4. Start-up sequence (measured order, times from the boot log)

1. I2C bus up (GPIO 7/8), scan, read ATTINY ID (expect 0xC3).
2. ATTINY: POWERON=0, 30 ms, PWM=0, PORTC=0 (all resets asserted), 10 ms,
   PORTA=0x04, 10 ms, PORTB=0x80, 10 ms, PORTC=0x01, 80 ms.
3. Enable the DPHY LDO (channel 3, 2500 mV).
4. DSI bus: 1 lane, 600 Mbps, `MIPI_DSI_PHY_CLK_SRC_DEFAULT`, `clock_lane_force_hs=1`.
   (`PLLF20M` as the PHY reference silently corrupts the link: always white, no error.)
5. Create and delete a DBI IO: this latches low-power generic writes, which the bridge
   needs and which persist.
6. Create the DPI panel (RGB888, 1 frame buffer in PSRAM), set video mode
   **non-burst with sync pulses**, per-frame ACK off, `esp_lcd_panel_init()` (this
   starts DMA and video), clock lane forced to HS, command ACK off.
7. Release the bridge: PORTC=0x0D, then the ATTINY proxy writes (ADDR_H=0x04,
   ADDR_L=0x7C, DATA_H=0, DATA_L=0, 7 ms apart), 100 ms settle.
8. Write the bridge registers in section 3.
9. Release touch reset: PORTC=0x0F. Backlight PWM=0xFF.
10. Start the touch task.

Video must be running before step 8: the bridge needs a live HS clock to receive LP
writes, and a packet sent while the link is idle is dropped with no host-visible error.

**Do not issue bridge readbacks.** They stop and restart DSI video mode at an arbitrary
point in a frame and have always returned no response. With them enabled, identical
firmware came up centred, then displaced, then centred across consecutive boots. With
them off (`PROBE_READBACK 0`, the default), five resets in a row and an earlier series
all came up the same. This is the best explanation found, not a proven root cause.

## 5. Pixel format

The framebuffer is written by one function, `fill_rect()`, in **[B][G][R]** byte order.
That is what the panel needed on 2026-10-25: the previous [R][B][G] order showed TL
blue, TR red, BL green for card colours TL red, TR green, BL blue. An earlier bring-up
measured a green/blue swap instead, so treat the order as something to re-check if the
link phase changes. The probe self-check compares drawn bytes with exact expectations;
a colour-order change must update both places.

## 6. Touch (FT5x06 at 0x38)

- Register map (FT5x06 family): 0x02 touch count in [3:0]; then per point from 0x03:
  XH (event [7:6], X[11:8]), XL, YH (id [7:4], Y[11:8]), YL. Idle reads `00 FF FF FF FF`.
- The controller reports in the panel's native 800x480 range (observed x 69..756 and
  y 5..475 across free touching) but **both axes are mirrored** relative to the display:
  up moved down and left moved right. `ft5x06_touch.h` maps `x -> 799-x`, `y -> 479-y`.
- This firmware reports almost only event 2 (contact) and rarely event 0 (down), so a
  press is "any touch after none", not event 0.
- Stationary-finger dropouts of 50 ms or more occur; a release is reported only after
  10 consecutive empty 10 ms polls (100 ms). With a 4-poll debounce, 9 of 46 presses
  still followed a release within 150 ms; without any debounce, 118 of 172.
- **Self-test result (measured, 2026-10-25):** 9 of 9 targets hit, per-target error
  dx -6..+15 px, dy -21..+1 px (means dx +3.2, dy -7.2), zero I2C errors, exactly one
  press and one release per target. The consistent upward bias is probably fingertip
  aim; it was not separated from a controller offset.
- API: `ft5x06_start(bus, tag)` then `ft5x06_get(&pt)` for the latest point. The OrcSDR
  app starts the task but has no touch UI yet.

## 7. Not verified

- Displacement in pixels: the image looked centred by eye and in one handheld video;
  there is no photo-based measurement with an uncertainty.
- Whether the bridge timing generator is live or inert (section 3).
- The root cause of the original left-edge wrap; the readback explanation is a hypothesis.
- A long soak of the OrcSDR app: heap was steady at 588800 B over the first 13 s only.
  The probe soak (about 10 min, 119 self-check passes, no errors) ran before touch.
- Behaviour next to the RTL-SDR driver: PSRAM scan-out is about 1.15 MB per frame at
  ~45 Hz and shares the bus with the IQ path; USB host plus DSI together has not been run.
- Cold power-up versus warm reset; the bench resets used RTS, with the panel powered.

## 8. References (kernel sources, facts only)

`drivers/gpu/drm/bridge/tc358762.c`, `drivers/regulator/rpi-panel-attiny-regulator.c`,
`drivers/gpu/drm/panel/panel-raspberrypi-touchscreen.c`; copies are kept with the bring-up
session files. Tomi Valkeinen's `tc358762-fixes` series is the source of the corrected
LCDCTRL bit layout (bit 1 VTGEN, bits 6:4 PXLFORM, bit 8 DPI_EN).
