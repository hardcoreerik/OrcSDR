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
   `clock_lane_force_hs`, programs the TC358762 bridge over DSI generic long
   writes, and starts DPI video.
4. Cycles the LCD controller test patterns, then restarts with the next timing
   profile. The profile index is held in RTC memory, so **one flash sweeps every
   candidate** and the log always states which profile is active.

## Measured state

Working, confirmed on hardware:

- MIPI DPHY LDO channel 3 @ 2500 mV
- One-lane DSI at 660 / 720 / 624 Mbps, `clock_lane_force_hs=1`, no driver errors
- ATTINY88 power sequence latches (PORTA `0x04`, PORTB `0x85`) and the
  **backlight turns on**
- TC358762 registers are written without error and `SYSCTRL`, `STARTPPI` and
  `STARTDSI` are applied

Not working yet:

- **No pixels.** The panel is bright white and backlit. `PORTC` never reads back
  the written value (always `0x00`), so reset release cannot be confirmed this
  way. Three timing profiles plus the non-burst-with-sync-pulses video mode
  override have been tried; none produced an image.

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
