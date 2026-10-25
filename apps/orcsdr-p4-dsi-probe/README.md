# OrcSDR ESP32-P4 DSI probe (Hosyond 7" panel)

Isolated hardware diagnostic. It shares no code with `orcsdr-tab5`,
`orcsdr-waveshare-4b` or OrcNode, so a failed experiment here cannot change a
supported target.

## Target hardware

- Waveshare **ESP32-P4-Module-DEV-KIT** — ESP32-P4 rev v1.3, 16 MB flash
- 7" 800x480 MIPI-DSI panel, 5V/GND from the 40-pin header plus the DSI ribbon
- Serial console on the CH343 UART bridge (COM8 on the development machine)

Our unit answers on the DSI-connector I2C bus (SDA=GPIO7, SCL=GPIO8, I2C_NUM_1)
at `0x18`, `0x38` and `0x45`. Register `0x80` on `0x45` returns `0xC3`, the
revision the mainline kernel driver `rpi-panel-attiny-regulator` accepts for a
Raspberry Pi 7" panel, so the module is treated as **Pi 7"-compatible with a
TC358762 bridge behind an ATTINY88**, not as a bare ICN6211 board.

## What the probe does

1. Scans and identifies the DSI-connector I2C bus (read-only register reads).
2. Powers the panel through the ATTINY88 controller: bridge power plus backlight.
3. Raises the MIPI DPHY LDO, creates a one-lane DSI bus with
   `clock_lane_force_hs` and `MIPI_DSI_PHY_CLK_SRC_DEFAULT`, creates the DPI
   panel with the active timing profile, calls `esp_lcd_panel_init()` to start
   DMA video, then releases the bridge reset, issues the ATTINY88 `SYSPMCTRL`
   wake proxy write, and programs the TC358762 over DSI generic long writes.
4. Draws one static test card (header, primary-colour blocks, colour bands, grey
   ramp, white frame) and re-submits it every 5 s. There is no automatic profile
   sweep: `H4_DEFAULT_PROFILE` selects the single active profile.

## Measured state

Working, confirmed on hardware:

- MIPI DPHY LDO channel 3 @ 2500 mV
- One-lane DSI at 600 Mbps, `clock_lane_force_hs=1`, no driver errors
- ATTINY88 power sequence latches (PORTA `0x04`, PORTB `0x85`) and the
  **backlight turns on**
- TC358762 registers are written without error and `SYSCTRL`, `STARTPPI` and
  `STARTDSI` are applied
- **Pixels are produced.** Video reaches the LCD.

The decisive fix was the DSI PHY reference clock source. With
`MIPI_DSI_PHY_PLLREF_CLK_SRC_PLL_F20M` the panel stayed white forever with no
host-visible error; `MIPI_DSI_PHY_CLK_SRC_DEFAULT` (with `lane_mbps = 600.0`)
produced an image immediately. A wrong PHY reference silently corrupts the link.

Not working yet:

- **Geometry.** The raster is vertically compressed and repeated: the frame
  appears twice, top and bottom, with a thin dark seam near mid-height, while
  horizontal content spans the full width correctly.
- **Colour.** Colours are washed out and dithered-looking. Part of this is
  camera moiré; the rest is unexplained.
- **Bridge verification.** Generic reads from the TC358762 return NO RESPONSE
  before and after init, so register application cannot be confirmed by
  readback. A submitted write is not proof of an applied write.
- `PORTC` never reads back the written value (always `0x00`) although writes
  demonstrably take effect (the backlight follows `PC_LED_EN`), so reset release
  cannot be confirmed by readback.

## Build and run

Requires **ESP-IDF 6.1** — `esp_lcd_dsi_bus_config_t.flags.clock_lane_force_hs`
does not exist in 5.5.x, and OrcSDR's pinned toolchain is deliberately not
changed by this probe.

```powershell
call C:\Espressif\v6.1\esp-idf\export.bat
cd apps\orcsdr-p4-dsi-probe
idf.py -p COM8 build flash
idf.py -p COM8 monitor
```

## Restoring the firmware that shipped on the board

The unit shipped with OrcNode 0.1.0-alpha (IDF v6.1). A full 16 MB dump taken
before the first write must be restored with:

```powershell
python -m esptool --port COM8 write-flash 0x0 stock-16MB-80f1b2d1e0d5.bin
```

Do not flash this probe to a unit whose stock image has not been dumped first.

## Provenance and licensing

Register facts come from the mainline kernel drivers
`drivers/regulator/rpi-panel-attiny-regulator.c` and
`drivers/gpu/drm/bridge/tc358762.c`, and from the public documentation of the
ESP32-P4 DSI support community.

The `ESP32-P4-DSI-Support-Hub` project is **CC BY-NC-ND 4.0** (no derivatives,
non-commercial): only hardware facts are used here, never its source. The
`kurozz/esp_lcd_rpi_dsi` component is **GPL-2.0-only** and is a reference only;
review is required before reusing any of its source.
