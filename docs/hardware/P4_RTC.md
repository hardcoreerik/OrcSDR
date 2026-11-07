# LilyGO T-Display-P4: battery-backed clock (PCF8563)

This page is specific to the `lilygo-t-display-p4-amoled` build. Other profiles declare no RTC
(`rtc_type = ORCSDR_RTC_NONE`) and behave exactly as before. The Tab5 keeps its own RX8130CE path
through the Tab5 time service; this is not shared with it.

## Hardware facts

| Fact | Value | Source |
| --- | --- | --- |
| Part | NXP PCF8563 | LilyGO `t_display_p4_config.h` @ a47cf73e (facts only) |
| Bus | I2C1, the panel bus (SDA 7, SCL 8), shared with the XL9535, GT9895 and BQ27220 | same |
| Address | 0x51 | same |
| Interrupt | XL9535 IO12 (unused: no alarm or timer is armed) | same |
| Register map, BCD fields, VL and century bits | NXP PCF8563 datasheet | datasheet |

No LILYGO driver code was read into or adapted for this feature (see `P4_REUSE.md` and the repository
licensing rule). The codec is `ports/p4/components/orcsdr_board_hosyond/include/pcf8563_protocol.h`.

## Behaviour

- The chip always holds **UTC**. The local time zone is applied by the existing UTC-offset setting.
- **Boot.** `orcsdr_rtc_start()` reads the chip. If it decodes, the VL flag is clear and the date is in 2024-01-01 up to
  2100-01-01, it waits for the chip's seconds to tick (at most 1.2 s) and sets the system clock then, so
  the system clock follows the chip to within a few milliseconds. If the system clock is already set it is left alone.
  A chip that was last set long ago is still trusted: it can be off by its drift, and the first network sync corrects it.
- **VL set (battery lost, or never set).** The chip's time is not trusted and the system clock is not touched until a
  network or manual set. The boot log says so.
- **After every SNTP step and every manual set** the network clock asks for the system clock to be copied into
  the chip. A one-shot timer fires just after the next whole second (the chip counts whole seconds and restarts its
  sub-second counter on a write), the callback writes the seven time registers with VL clear, reads them back and
  verifies. If it wakes late, mid-second, it re-arms up to three times and otherwise counts a failed write rather
  than writing a fractional second; the next sync retries.
- A write never blocks the caller (the network clock or the UI) and **there is no task**. A first version used a
  worker task with a 12 KB stack; on this board that left too little internal/DMA RAM and the C6 SDIO bring-up failed
  with `sdmmc_allocate_aligned_buf: not enough mem`. Do not add a task or static buffers here without re-checking C6 startup.
- Control 1 is checked at start; if STOP, TEST1 or TESTC is set it is cleared and logged.

## What the build does not do

- No alarm or timer, so the interrupt line is unused.
- No drift correction. A PCF8563 is typically within a few ppm at room temperature; the network sync is the reference.
- No dashboard layout, Settings page or gesture changed. The Settings clock page still reads the system clock.
- Years 2000-2099 only (century bit clear); a chip with the century bit set is rejected.

## Evidence

| Check | Result |
| --- | --- |
| `python tests/test_pcf8563_protocol.py` (MSVC, `/W4 /WX`) | 7 passed: BCD masks, ranges, calendar edges, leap years, century bit, VL, epoch round trips against Python `calendar` |
| `wsl bash tools/test-p4-network-clock.sh` | passes with the new hook compiled out in the test build |
| LilyGO firmware build | see the session record |
| Physical: chip answers at 0x51, VL state on a fresh unit, restore after power cycle, write after NTP | **open** (needs the board; see acceptance below) |

## Physical acceptance (open)

1. Boot with Wi-Fi available: log shows `RTC: PCF8563 at 0x51`, then a restore line or a voltage-low warning.
2. After the first NTP sync: `PCF8563 set from system clock`.
3. Power off (USB and battery unplugged long enough to be sure), power on with **no Wi-Fi**: the log shows
   `system clock restored from PCF8563` and the Settings clock shows the right time.
4. Disconnect the battery and power: the next boot logs the voltage-low warning (confirms the backup path).
5. Unknown until measured: whether this unit's backup supply keeps the RTC running with only USB removed.
