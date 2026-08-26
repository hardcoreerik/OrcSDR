#!/usr/bin/env python3
"""Capture all live Tab5 RF visualizers with one radio-stop transfer window."""

from __future__ import annotations

import argparse
import re
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from capture_landing_dashboards import bmp_to_png  # noqa: E402
from help_media import Tab5  # noqa: E402

VIEWS = [
    ("spectrum", "vis01"),
    ("waterfall", "vis02"),
    ("phosphor", "vis03"),
    ("spectrum3d", "vis04"),
    ("constellation", "vis05"),
    ("iqscope", "vis06"),
    ("polar", "vis07"),
    ("occupancy", "vis08"),
    ("peakavg", "vis09"),
    ("doppler", "vis10"),
    ("channelizer", "vis11"),
    ("audiospec", "vis12"),
]


def self_check() -> None:
    assert len(VIEWS) == 12
    assert len({view for view, _ in VIEWS}) == len(VIEWS)
    assert len({slug for _, slug in VIEWS}) == len(VIEWS)
    assert all(re.fullmatch(r"[a-z0-9]{1,8}", slug) for _, slug in VIEWS)


def auth_send(client: Tab5, command: str, prefixes: tuple[str, ...], timeout: int = 20) -> str:
    for attempt in range(3):
        try:
            client.authenticate()
            break
        except TimeoutError:
            if attempt == 2:
                raise
            time.sleep(1)
    client.send(command)
    return client.wait(prefixes, timeout)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM17")
    parser.add_argument("--pairing-key", type=Path, default=ROOT / ".orclink" / "ui-doc.key")
    parser.add_argument("--settle-seconds", type=float, default=4.0)
    parser.add_argument("--output", type=Path, default=ROOT / "artifacts" / "visualizer-captures")
    parser.add_argument("--view", action="append", choices=[view for view, _ in VIEWS])
    parser.add_argument("--self-check", action="store_true")
    args = parser.parse_args()
    self_check()
    if args.self_check:
        print("capture_visualizers self-check: PASS")
        return

    raw = args.output / "raw"
    raw.mkdir(parents=True, exist_ok=True)
    selected = VIEWS if not args.view else [item for item in VIEWS if item[0] in args.view]
    client = Tab5(args.port, args.pairing_key)
    captures: list[tuple[str, str, str, str]] = []
    band = "FM"
    frequency = 0
    stopped = False
    try:
        client.send("RTL_FREQ")
        line = client.wait(("RTL_FREQ_STATUS",), 10)
        match = re.search(r"band=(\S+) frequency_hz=(\d+)", line)
        if not match:
            raise RuntimeError(f"Malformed frequency status: {line}")
        band, frequency = match.group(1), int(match.group(2))
        for index, (view, slug) in enumerate(selected, 1):
            opened = auth_send(client, f"RTL_VIS OPEN {view}", ("RTL_VIS_OK", "RTL_VIS_ERROR"))
            if opened != "RTL_VIS_OK":
                raise RuntimeError(f"{view}: {opened}")
            auth_send(client, "RTL_VIS FREEZE OFF", ("RTL_VIS_OK", "RTL_VIS_ERROR"))
            time.sleep(max(0.0, args.settle_seconds))
            client.send("RTL_VIS STATUS")
            status = client.wait(("RTL_VIS_STATUS",), 10)
            if f"view={view}" not in status or "source=1" not in status:
                raise RuntimeError(f"{view}: {status}")
            auth_send(client, "RTL_VIS FREEZE ON", ("RTL_VIS_OK", "RTL_VIS_ERROR"))
            result = auth_send(client, f"UI_CAPTURE {slug}",
                               ("UI_CAPTURE_DONE", "UI_CAPTURE_ERROR"), 45)
            if not result.startswith("UI_CAPTURE_DONE"):
                raise RuntimeError(f"{view}: {result}")
            remote = re.search(r'path="([^"]+)"', result).group(1)
            digest = re.search(r"sha256=([0-9a-f]{64})", result).group(1)
            captures.append((view, slug, remote, digest))
            auth_send(client, "RTL_VIS FREEZE OFF", ("RTL_VIS_OK", "RTL_VIS_ERROR"))
            print(f"[{index}/{len(selected)}] saved {view}: {status}")

        auth_send(client, "RTL_STOP", ("RTL_STOPPING",))
        stop = client.wait(("RTL_STOP_RESULT",), 15)
        if stop != "RTL_STOP_RESULT ESP_OK":
            raise RuntimeError(stop)
        stopped = True
        for index, (view, slug, remote, expected) in enumerate(captures, 1):
            bmp = raw / f"{slug}-{view}.bmp"
            png = args.output / f"{slug}-{view}.png"
            actual = client.get_file(remote, bmp)
            if actual != expected:
                raise RuntimeError(f"{view}: capture/transfer hash mismatch")
            bmp_to_png(bmp, png)
            print(f"[{index}/{len(captures)}] downloaded {png.name} sha256={actual}")
    finally:
        try:
            if stopped and frequency:
                auth_send(client, f"RTL_TUNE {band} {frequency}",
                          ("RTL_TUNE_OK", "RTL_TUNE_UNAVAILABLE"))
                time.sleep(3)
            if captures:
                auth_send(client, "RTL_VIS FREEZE OFF", ("RTL_VIS_OK", "RTL_VIS_ERROR"))
                auth_send(client, "RTL_VIS CLOSE", ("RTL_VIS_OK", "RTL_VIS_ERROR"))
        finally:
            client.close()


if __name__ == "__main__":
    main()
