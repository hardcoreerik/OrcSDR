# Troubleshooting

## No audio

Confirm sound is enabled, volume is nonzero, and the dashboard reports an active receiver. Leave and re-enter the audio dashboard. RF Health should show no audio underruns or sustained ring pressure.

After a Wi-Fi scan/connect reset, the FM dashboard should reopen itself and force-restart the speaker (`RTL_SPEAKER_RESUME`). If the codec stays silent, leave FM and enter it again; that path calls `Speaker.end()` then `begin()` so the ES8388 amp is re-enabled. Boot logs `RTL_SPEAKER_RESUME ok=1`.

## RTL-SDR offline

Cold-boot with stable power and the dongle disconnected, then attach it after the Tab5 reaches its normal startup state. If the device repeatedly resets, capture a high-speed serial log before changing firmware.

## Wi-Fi scan or connection fails

Confirm Wi-Fi power is enabled and choose the correct internal or external antenna. Scanning should coexist with reception. A crash or restart is a regression; preserve the serial log and reset reason.

On-chip DMA RAM is split so Wi-Fi and the radio do not steal each other's room: ESP-Hosted (C6 Wi-Fi) and the I2S speaker stay in reserved internal DMA; RTL-SDR USB URBs stay in PSRAM. Boot logs `RTL_DRAM_BUDGET`. After Wi-Fi starts, `dma_largest` should stay above ~20 KiB so I2S can still start. A scan/connect panic with `HS_MP: mempool create failed` or `sdio_mempool_create` means the Hosted slice was gone — keep `CONFIG_ESP_HOSTED_USE_MEMPOOL=n` and do not raise `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` above 8 KiB.

## Boot resets

Do not assume every reset is a power failure. Record the reset reason, power telemetry, USB-host stage, and whether a depleted external battery was attached.

On the Tab5, `ESP_RST_BROWNOUT` during live radio often tracks the **PC flashing cable** (ESP32-P4 USB Serial/JTAG, `VID_303A`/`PID_1001`, bench `COM17`), not a generally weak battery or wall supply. That cable injects PC USB VBUS and keeps the USB-JTAG device enumerated. Windows opening the COM port can also pulse DTR/RTS. The same unit is typically stable when that cable is unplugged and the Tab5 runs from its own battery or USB-C PD path.

Firmware leaves the P4 brownout detector **off** (`CONFIG_ESP_BROWNOUT_DET=n`) so a VBUS sag no longer resets the chip. Boot logs `RTL_BROWNOUT det=off reset=off`. The sag can still glitch USB host, Wi-Fi, audio, or flash writes; that is accepted. Unplugging the PC Serial/JTAG cable after flash remains the clean living-room path.

For living-room / Android TV use: flash, then unplug the PC Serial/JTAG cable. Control the radio over Wi-Fi. Do not leave a serial monitor open on the flash port.

## Missing SD data

Check SD readiness and free space in Settings. Database, map, capture, and screenshot operations retain valid files until a replacement has been verified.

## Weak signals

Use an antenna suitable for the frequency, minimize feed-line loss, and compare relative dBFS readings on the same hardware. Relative readings are not calibrated dBm.
