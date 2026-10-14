# Waveshare ESP32-P4-WIFI6-Touch-LCD-4B — USB Host/Power investigation

## 2026-10-14 observed blocker

User reports no SDR dongle is detected from the physical board. **VBUS has
not been measured**: it is NOT yet established that the board leaves OTG
unpowered, nor that the USB driver is at fault.

Authoritative hardware references:
- https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-4B
- https://files.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-4B/ESP32-P4-WIFI6-Touch-LCD-4B.pdf
- https://github.com/waveshareteam/Waveshare-ESP32-components/blob/master/bsp/esp32_p4_wifi6_touch_lcd_4b/esp32_p4_wifi6_touch_lcd_4b.c

## Physical interfaces are NOT equivalent

- `USB TO UART`: USB-C -> CH343P, serial console/flashing and primary
  5 V supply. **Not an RTL-SDR Host port.**
- `USB OTG`: second USB-C -> the ESP32-P4's native USB HS
  `USBD_P/USBD_N` data pair; the SDR must use THIS port.
- The schematic separately labels `USB0_5V` (UART) and `USB1_5V`
  (OTG), and shows a power transistor stage `Q3 AO3401` and a 5 V
  rail connection at OTG. The schematic alone does not prove that a
  specific USB-C cable/adapter will negotiate/supply host VBUS.
  Type-C CC/orientation and hardware power path should be verified.

## Firmware ownership

The Waveshare BSP's `bsp_usb_host_start()` calls `usb_host_install()`
and starts a USB host event task. The two power-mode parameters are not
used in its implementation: no GPIO VBUS-enabling write occurs there.
OrcSDR's `esp_rtl_sdr` already calls `usb_host_install()`. NEVER
start both USB host owners; BSP helper is not a missing VBUS switch.

The diagnostic Home build logs `HOME_4B USB_DIAG` every five scans or
when a raw USB device is visible:
- `raw_devices`: USB enumeration at the bus level, INCLUDING any hub,
  NOT necessarily an RTL-SDR dongle.
- `accepted_sdr`: recognized RTL-SDR profiles after scan.
- `raw`, `scan`: API result names; `safe_mode`: driver fault guard.

No USB devices detected is consistent with no port power, wrong cable/role,
a bad connector, incompatible adapter, or enumeration failure. A raw device
with zero accepted profiles favors profile/identity/descriptor issues.

## Controlled bench steps

1. Leave the board powered and monitored through **USB TO UART**.
   Connect SDR ONLY on the physically marked **USB OTG** connector;
   verify not using a charge-only cable or inappropriate C-to-C adapter.
2. Without the SDR, use a correct USB-C inline power meter or USB-A
   adapter breakout on OTG. Measure **VBUS vs GND** on the peripheral
   side, preferably with a modest test load. Expect ~5 V for a
   functioning powered host connection. Do not short pins or inject raw
   external 5 V into the board/OTG connector.
3. If 0 V, compare the UART 5 V supply and OTG 5 V. Investigate host
   adapter's USB-C CC role, power switching path and board revision.
   Use a properly self-powered USB 2.0 hub with upstream OTG connection
   only if it does not backfeed VBUS. A powered hub alone does NOT prove
   an upstream host connection if its upstream-side VBUS detection is missing.
4. If ~5 V, test a known-working low-power USB peripheral; capture raw
   USB counts and USB Host serial diagnostics. Do not call its absence an
   SDR decoder problem unless the USB stack sees the device.
5. If `raw_devices>0` and `accepted_sdr=0`, inspect
   descriptors/enumeration errors and accepted SDR profiles. If
   `accepted_sdr>0` and start fails, inspect claim/rate/URB errors.
6. Record board silicon revision, source of power, adapter/cable model,
   VBUS measurement, USB status markers and 5-minute IQ metrics.
   Save serial logs in the same directory used to run testing commands.

**No unverified GPIO power writes, C6 flashing, solder bridges, or blind
power injection.** Do not infer port power from ESP-IDF compilation.
