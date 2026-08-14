#!/usr/bin/env python3
"""Small dependency-free validation for the public OrcSDR guide."""

from __future__ import annotations

import hashlib
import json
import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DOCS = ROOT / "docs" / "user-guide"
MANIFEST = ROOT / "docs" / "help_media" / "manifest.json"


def png_size(path: Path) -> tuple[int, int]:
    with path.open("rb") as source:
        header = source.read(24)
    if header[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"Not a PNG: {path}")
    return struct.unpack(">II", header[16:24])


def main() -> None:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    ids = [screen["id"] for screen in manifest["screens"]]
    assert len(ids) == len(set(ids)), "Duplicate documentation screen ID"
    for markdown in DOCS.rglob("*.md"):
        text = markdown.read_text(encoding="utf-8")
        for alt, target in re.findall(r"!\[([^]]*)\]\(([^)]+)\)", text):
            assert alt.strip(), f"Missing image alt text: {markdown}"
            resolved = (markdown.parent / target.split("#", 1)[0]).resolve()
            assert resolved.exists(), f"Missing image: {markdown} -> {target}"
        for target in re.findall(r"(?<!!)\[[^]]+\]\(([^)]+)\)", text):
            if "://" in target or target.startswith("#"):
                continue
            resolved = (markdown.parent / target.split("#", 1)[0]).resolve()
            assert resolved.exists(), f"Broken local link: {markdown} -> {target}"
    clean = DOCS / "assets" / "screenshots" / "clean"
    if clean.exists():
        hashes = {}
        for image in clean.glob("*.png"):
            assert png_size(image) == (1280, 720), f"Wrong dimensions: {image}"
            digest = hashlib.sha256(image.read_bytes()).hexdigest()
            assert digest not in hashes, f"Duplicate captures: {hashes.get(digest)} and {image}"
            hashes[digest] = image
        captured = [screen for screen in manifest["screens"] if screen.get("capture_sha256")]
        assert len(hashes) == len(captured), "Capture manifest and committed clean PNG count differ"
    print(f"HELP_DOCS_VALIDATE_OK screens={len(ids)}")


if __name__ == "__main__":
    main()
