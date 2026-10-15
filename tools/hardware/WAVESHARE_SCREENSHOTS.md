# Waveshare screenshots

The Waveshare firmware accepts `ORCUI_SCREENSHOT <16-hex-token> current` on its
existing 115200-baud UART console at any time. It snapshots the active LVGL
screen into an independent PSRAM buffer while holding the display lock, then
releases the lock before PNG encoding and serial transmission. A dedicated
low-priority task handles requests; USB/IQ callbacks never capture or encode.
One request runs at a time. No HTTP server, SD card or new dependency version
is required. The pinned libpng 1.6.58~2 is reused.

From the repository root, using Python with pyserial:

```powershell
python tools/hardware/capture_4b_screenshot.py --port COM40
python tools/hardware/capture_4b_screenshot.py --port COM40 --view home
python tools/hardware/capture_4b_screenshot.py --port COM40 --view drawer
```

Use the positively identified port; the helper never selects a device, resets
it or flashes firmware. Close any competing serial monitor first. The default
`current` view does not change UI state. Adaptive Home additionally supports
`home` and `drawer`, which select that rendered state before capture; these are
software screenshot actions, not proof of a physical touch test. Legacy mode
supports `current` only.

The PC saves validated PNGs and JSON provenance sidecars to
`artifacts/screenshots/waveshare-4b/`, using UTC timestamps, port and view in the
filename. Override with `--output-dir` if needed. The sidecar includes dimensions,
transfer CRC32 and the running application's full ELF hash. Ordinary telemetry
can interleave with the numbered, token-tagged base64 chunks. Missing chunks,
length/CRC mismatches, invalid PNG chunks or timeouts fail without saving a PNG.
UART transfer can take tens of seconds; display locking covers only snapshot
rendering, not encoding or transmission.

After every Waveshare flash, capture at least Home and drawer using this helper
and visually inspect both PNGs. Record paths with the flash/build evidence.
These are actual firmware-rendered screenshots, not host simulations. They
cannot verify panel wiring, backlight, touch calibration or color errors arising
after LVGL rendering; owner observations still cover those physical properties.

Protocol tests:

```powershell
python -m unittest discover -s tools/hardware -p test_capture_4b_screenshot.py -v
```
