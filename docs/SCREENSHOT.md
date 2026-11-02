# OrcSDR screenshots

Use the authenticated serial command `SCREENSHOT` (case insensitive), or:

```
python tools/screenshot.py --port COM32 --baud 921600 capture
```

The tool writes `capture.png`, verifies its length and SHA-256, and does not
retune or stop the receiver. PNG encoding occupies the UI task briefly.
Python callers can import `screenshot` from `tools/screenshot.py`; it accepts
an existing authenticated transport, pairing key, and output filename.

`FT8 SNAP` remains a firmware alias. `tools/tab5_ft8_shot.py` remains an import
compatibility wrapper; its `snap_png` function uses the old command for older
firmware. New workflows use `screenshot.py` and `SCREENSHOT`.
The optional `--tab` and `--open` arguments still select FT8 views explicitly;
they do not limit ordinary screenshots to FT8. The legacy `--sd` path retains
its documented radio interruption and is never selected automatically.
