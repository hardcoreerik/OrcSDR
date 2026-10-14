# Waveshare ESP32-P4-WIFI6-Touch-LCD-4B — USB Host/Power investigation

## 2026-10-14 measured-source update

**Direct hardware observation**: With an inline USB-C power meter, the user
observes that **either Type-C port powers the Waveshare board as an input**,
but neither port appeared to **supply 5 V outward**. The exact meter, cable,
connected downstream load, CC-attach behavior and voltage readings were not
specified, so record this as observation, not as a calibrated 0 V assertion.

**Schematics support a board-level USB-C power-role mismatch:**

- Official main-board schematic (page 1, USB block):
  https://files.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-4B/ESP32-P4-WIFI6-Touch-LCD-4B.pdf
- H1 `USB TO UART`: `R1/R4 = 5.1 kΩ` on CC pins.
- H2 `USB OTG`: `R2/R3 = 5.1 kΩ` on CC pins.
- The 5.1 kΩ CC-to-ground termination is `Rd` for **USB Type-C sink**,
  rather than `Rp` used by a Type-C source/host power provider. These
  are fixed hardware resistors, not an ESP32-controlled dual-role Type-C
  policy engine. Both USB-C ports being usable for board input power is
  consistent with this implementation.
- The separately labeled `USB0_5V` and `USB1_5V` lines feed board
  `VCC_5V` through transistor networks including `Q2/Q3 AO3401` and
  `U3/U4 MMDT3906DW`. This looks like input power path control/ORing,
  **not evidence** of an available software-controlled 5 V VBUS source
  switch. Exact power direction/current blocking on a physical PCB
  revision needs an electrical test.
- Official Espressif Type-C guidance:
  https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/usb_overview/usb_typec_hardware_guide.html
  Source uses Rp, sink uses Rd, and two sink-only CC endpoints do not
  negotiate attachment.
- In Waveshare's current 4B BSP, `bsp_usb_host_start(mode,limit_500mA)`
  only starts the USB host library/task; its named power arguments are
  unused. The `esp_rtl_sdr` driver already owns that library. Calling both
  will create duplicate host owners and cannot correct Type-C hardware.

**Working hypothesis**: the board offers the P4's HS USB host **data
controller** on the Type-C OTG socket, but the socket is physically configured
to receive VBUS rather than source/advertise it. This can prevent a
bus-powered RTL-SDR or common OTG adapter from ever powering or attaching.
It is not proof the USB HS pins/controller/receiver driver are defective.

**Important firmware trap**: Espressif
`usb_host_lib_set_root_port_power(true)` affects the host-controller's
logical root-port state; it is NOT a guarantee of physical 5 V on the
connector. Do not propose it as a VBUS power supply.

### Revised first physical checks — do NOT inject raw 5 V

1. Confirm which port is `USB OTG`. Keep `USB TO UART` on the PC for
   live serial logs and stable power.
2. Record exact USB-C meter model/connection orientation and whether
   a load was attached; some inline meters display 0/no reading if
   proper Type-C attachment/negotiation is absent.
3. With electronics test equipment and suitable connector breakout,
   measure OTG receptacle VBUS-to-GND with a known valid downstream
   attach/test load and then when the other USB port powers the board.
   A 0 V reading supports missing source power; an isolated meter
   reading alone is not a definitive hardware fault.
4. For a powered-SDR test, use a **verified, purpose-designed**
   USB-C host-data/power adapter or suitable externally powered
   USB 2.0 HS hub arrangement that handles the board's *sink-side CC
   role* and supplies the peripheral from its own isolated 5 V supply.
   Standard self-powered hubs and generic USB-C OTG dongles may **not**
   solve this mismatch; check upstream VBUS detection and CC roles.
   Do not connect multiple 5 V sources together or backfeed a PC,
   charger or board. Do not modify CC resistors or transistor paths
   without a reviewed design and measured PCB revision.
5. After proper role, power and wiring are verified, compare USB raw
   device counts vs accepted RTL-SDR counts in `HOME_4B USB_DIAG`.
   With `raw_devices=0`, fix hardware/power/host enumeration before
   changing the tuner driver.
6. Capture evidence and link it to the specific physical board rev.

**User-facing conclusion**: Hardware is not yet qualified as a
plug-and-play RTL-SDR appliance. The native HS USB host capability may
be usable with a correct powered adapter, but ordinary Type-C plug-in
operation remains an open hardware validation gate.

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
   side, preferably with a modest test load. A 5 V source reading is only expected if the
   connector and attached setup correctly implement a source-side power role. Do not short pins or inject raw
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
