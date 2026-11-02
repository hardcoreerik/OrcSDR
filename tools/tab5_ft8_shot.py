#!/usr/bin/env python3
"""Compatibility entry point. Use tools/screenshot.py for new workflows."""
from screenshot import *  # noqa: F401,F403
from screenshot import screenshot as _screenshot

def snap_png(dev, key, destination, tries=3):
    # Existing imports also work against firmware predating SCREENSHOT.
    return _screenshot(dev,key,destination,tries,command="FT8 SNAP")

if __name__ == "__main__":
    main()
