#!/usr/bin/env python3
"""Capture completed Tab5 dashboards. Wait 10s after every screen change."""

from __future__ import annotations

import argparse
import shutil
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from help_media import Tab5  # noqa: E402

# Slugs must fit FAT 8.3 (this Tab5 card has no long names).
SCREENS = [
    ("home", "home"),
    ("settings.connectivity", "set0"),
    ("settings.location-adsb", "set1"),
    ("settings.data-maps", "set2"),
    ("settings.display-audio", "set3"),
    ("settings.radio-defaults", "set4"),
    ("settings.storage", "set5"),
    ("settings.companion", "set6"),
    ("settings.system", "set7"),
    ("fm.listen", "fm0"),
    ("fm.spectrum", "fm1"),
    ("fm.station-rds", "fm2"),
    ("fm.rf-health", "fm3"),
    ("fm.settings", "fm4"),
    ("p25.monitor", "p250"),
    ("p25.spectrum", "p251"),
    ("p25.talkgroups", "p252"),
    ("p25.program", "p253"),
    ("p25.rf-health", "p254"),
    ("adsb.radar", "ad0"),
    ("adsb.list", "ad1"),
    ("adsb.target", "ad2"),
    ("adsb.stats", "ad3"),
    ("adsb.settings", "ad4"),
    ("lora.overview", "lo0"),
    ("lora.nodes", "lo1"),
    ("lora.traffic", "lo2"),
    ("lora.map", "lo3"),
    ("lora.rf-health", "lo4"),
]


def show_and_capture(client: Tab5, screen_id: str, slug: str, settle: float, dest_dir: Path) -> Path:
    modes = ("live", "demo")
    shown = None
    for mode in modes:
        client.authenticate()
        client.send(f"UI_DOC_SHOW {screen_id} {mode}")
        shown = client.wait(("UI_DOC_SHOW_DONE", "UI_DOC_ERROR"), 20)
        if shown.startswith("UI_DOC_SHOW_DONE"):
            break
    if shown is None or shown.startswith("UI_DOC_ERROR"):
        raise RuntimeError(f"{screen_id}: {shown}")
    print(f"  shown {shown}; waiting {settle:.0f}s")
    time.sleep(settle)
    result = ""
    for attempt in range(3):
        client.authenticate()
        client.send(f"UI_CAPTURE {slug}")
        result = client.wait(("UI_CAPTURE_DONE", "UI_CAPTURE_ERROR"), 45)
        if result.startswith("UI_CAPTURE_DONE"):
            break
        print(f"  capture retry {attempt + 1}: {result}")
        time.sleep(2)
    if not result.startswith("UI_CAPTURE_DONE"):
        raise RuntimeError(f"{screen_id}: {result}")
    remote = __import__("re").search(r'path="([^"]+)"', result).group(1)
    bmp = dest_dir / f"{slug}.bmp"
    try:
        client.get_file(remote, bmp)
    except RuntimeError:
        client.get_file(f"/orcsdr/{slug}.bmp", bmp)
    print(f"  captured {result}")
    return bmp


def bmp_to_png(bmp: Path, png: Path) -> None:
    from PIL import Image
    png.parent.mkdir(parents=True, exist_ok=True)
    with Image.open(bmp) as image:
        rgb = image.convert("RGB")
        if rgb.size != (1280, 720):
            raise ValueError(f"{bmp}: expected 1280x720, got {rgb.size}")
        rgb.save(png, optimize=True)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM17")
    parser.add_argument("--settle-seconds", type=float, default=10.0)
    parser.add_argument("--pairing-key", type=Path, default=ROOT / ".orclink" / "ui-doc.key")
    args = parser.parse_args()
    raw_dir = ROOT / "artifacts" / "dashboard-captures" / "raw"
    png_dir = ROOT / "docs" / "images" / "dashboards"
    raw_dir.mkdir(parents=True, exist_ok=True)
    png_dir.mkdir(parents=True, exist_ok=True)
    client = Tab5(args.port, args.pairing_key)
    try:
        client.authenticate()
        firmware, device_screens = client.doc_list()
        print(f"firmware={firmware} screens={len(device_screens)}")
        for index, (screen_id, slug) in enumerate(SCREENS, 1):
            if screen_id not in device_screens:
                print(f"[{index}/{len(SCREENS)}] skip missing {screen_id}")
                continue
            png = png_dir / f"{screen_id.replace('.', '-')}.png"
            if png.exists():
                print(f"[{index}/{len(SCREENS)}] skip existing {screen_id}")
                continue
            print(f"[{index}/{len(SCREENS)}] {screen_id} slug={slug}")
            bmp = show_and_capture(client, screen_id, slug, args.settle_seconds, raw_dir)
            bmp_to_png(bmp, png)
    finally:
        try:
            client.authenticate()
            client.send("UI_DOC_EXIT")
            client.wait(("UI_DOC_EXIT_DONE", "UI_DOC_ERROR"), 10)
        except Exception as error:
            print(f"warning: UI_DOC_EXIT failed: {error}", file=sys.stderr)
        client.close()

    landing = ROOT / "docs" / "images"
    home = png_dir / "home.png"
    lora = png_dir / "lora-overview.png"
    if home.exists():
        shutil.copyfile(home, landing / "orcsdr-tab5_2.png")
        print("updated docs/images/orcsdr-tab5_2.png from home")
    if lora.exists():
        shutil.copyfile(lora, landing / "orcsdr-tab5_3.png")
        print("updated docs/images/orcsdr-tab5_3.png from lora-overview")


if __name__ == "__main__":
    main()
