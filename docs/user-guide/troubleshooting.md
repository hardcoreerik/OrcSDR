# Troubleshooting

## No audio

Confirm sound is enabled, volume is nonzero, and the dashboard reports an active receiver. Leave and re-enter the audio dashboard. RF Health should show no audio underruns or sustained ring pressure.

## RTL-SDR offline

Cold-boot with stable power and the dongle disconnected, then attach it after the Tab5 reaches its normal startup state. If the device repeatedly resets, capture a high-speed serial log before changing firmware.

## Wi-Fi scan or connection fails

Confirm Wi-Fi power is enabled and choose the correct internal or external antenna. Scanning should coexist with reception. A crash or restart is a regression; preserve the serial log and reset reason.

## Boot resets

Do not assume every reset is a power failure. Record the reset reason, power telemetry, USB-host stage, and whether a depleted external battery was attached.

## Missing SD data

Check SD readiness and free space in Settings. Database, map, capture, and screenshot operations retain valid files until a replacement has been verified.

## Weak signals

Use an antenna suitable for the frequency, minimize feed-line loss, and compare relative dBFS readings on the same hardware. Relative readings are not calibrated dBm.
