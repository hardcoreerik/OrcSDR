#!/usr/bin/env python3
"""
Generate OrcSDR Tab5 ORSPLASH v1 asset from a still image.

Produces a seamless 10 s / 60 FPS / 600-frame baseline-JPEG loop (1280x720)
with mathematically periodic Ken-Burns + glow so frame 0 == frame 600 phase.

Outputs (under --out-dir):
  OrcSDR_Splash_1280x720_60fps_10s.orsplash
  OrcSDR_Splash_1280x720_60fps_10s_Poster.jpg
  OrcSDR_Splash_1280x720_60fps_10s_index.csv  (payload-relative offsets)
  OrcSDR_Splash_1280x720_60fps_10s.mjpeg      (optional raw concat)

Usage:
  python tools/generate_orcsdr_splash.py ^
    --src "F:\\Ai\\TheVisualizer\\Sample Images\\ChatGPT Image Jul 29, 2026, 12_55_56 PM (5).png" ^
    --out-dir docs/splash
"""

from __future__ import annotations

import argparse
import math
import struct
import sys
from pathlib import Path

from PIL import Image, ImageEnhance, ImageFilter


W, H = 1280, 720
FPS = 60
FRAME_COUNT = 600
MAGIC = b"ORCSPLSH"
HEADER_SIZE = 32
# flags: bit0=baseline JPEG, bit1=JPEG 4:2:2 (we leave bit1 clear; baseline is enough)


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Build OrcSDR ORSPLASH splash asset")
    p.add_argument("--src", required=True, type=Path, help="Source still image")
    p.add_argument(
        "--out-dir",
        type=Path,
        default=Path("docs/splash"),
        help="Output directory",
    )
    p.add_argument("--quality", type=int, default=82, help="JPEG quality 1-95")
    p.add_argument("--no-mjpeg", action="store_true", help="Skip raw .mjpeg dump")
    return p.parse_args()


def prepare_base(src: Path) -> Image.Image:
    im = Image.open(src).convert("RGB")
    # Oversized canvas so pan/zoom never exposes edges.
    cover_w, cover_h = 1480, 860
    scale = max(cover_w / im.width, cover_h / im.height)
    nw, nh = max(1, int(im.width * scale + 0.5)), max(1, int(im.height * scale + 0.5))
    im = im.resize((nw, nh), Image.Resampling.LANCZOS)
    left = (nw - cover_w) // 2
    top = (nh - cover_h) // 2
    return im.crop((left, top, left + cover_w, top + cover_h))


def frame_from_phase(base: Image.Image, phase: float) -> Image.Image:
    """phase in [0, 2π). Fully periodic → seamless loop."""
    # Gentle Ken Burns that returns to identity at phase 0 and 2π.
    zoom = 1.0 + 0.055 * math.sin(phase)
    pan_x = 36.0 * math.sin(phase)  # px on cover canvas
    pan_y = 18.0 * math.cos(phase) - 18.0  # 0 at phase 0
    glow = 0.90 + 0.12 * (0.5 + 0.5 * math.sin(phase * 2.0))  # 2-lobe shimmer

    cw, ch = base.size
    crop_w = W / zoom
    crop_h = H / zoom
    cx = cw * 0.5 + pan_x
    cy = ch * 0.5 + pan_y
    left = cx - crop_w * 0.5
    top = cy - crop_h * 0.5
    # Clamp crop inside cover
    left = max(0.0, min(left, cw - crop_w))
    top = max(0.0, min(top, ch - crop_h))
    box = (left, top, left + crop_w, top + crop_h)
    frame = base.resize((W, H), Image.Resampling.LANCZOS, box=box)

    # Soft bloom pulse on bright cyan peaks (seamless).
    frame = ImageEnhance.Brightness(frame).enhance(glow)
    frame = ImageEnhance.Contrast(frame).enhance(1.0 + 0.08 * math.sin(phase))
    # Subtle unsharp to keep peaks crisp after resize.
    frame = frame.filter(ImageFilter.UnsharpMask(radius=1.2, percent=80, threshold=2))
    return frame


def write_orsplash(
    frames_jpeg: list[bytes],
    out_path: Path,
) -> None:
    index_offset = HEADER_SIZE
    index_bytes = FRAME_COUNT * 8
    data_offset = index_offset + index_bytes
    payload = b"".join(frames_jpeg)
    flags = 0x0001  # baseline JPEG

    header = struct.pack(
        "<8sHHHHHHIII",
        MAGIC,
        1,  # version
        W,
        H,
        FPS,
        FRAME_COUNT,
        flags,
        index_offset,
        data_offset,
        len(payload),
    )
    assert len(header) == HEADER_SIZE

    index = bytearray()
    offset = 0
    for blob in frames_jpeg:
        index += struct.pack("<II", offset, len(blob))
        offset += len(blob)
    assert len(index) == index_bytes

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("wb") as f:
        f.write(header)
        f.write(index)
        f.write(payload)

    print(f"Wrote {out_path} ({out_path.stat().st_size / (1024 * 1024):.2f} MiB)")


def main() -> int:
    args = parse_args()
    if not args.src.is_file():
        print(f"ERROR: source not found: {args.src}", file=sys.stderr)
        return 1

    print(f"Loading {args.src} …")
    base = prepare_base(args.src)
    print(f"Cover canvas {base.size[0]}x{base.size[1]} → frames {W}x{H} x {FRAME_COUNT}")

    out_dir = args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)
    stem = "OrcSDR_Splash_1280x720_60fps_10s"

    frames_jpeg: list[bytes] = []
    csv_lines = ["frame,byte_offset,jpeg_length"]
    mjpeg_path = out_dir / f"{stem}.mjpeg"
    mjpeg_f = None if args.no_mjpeg else mjpeg_path.open("wb")
    payload_off = 0

    try:
        for i in range(FRAME_COUNT):
            phase = (2.0 * math.pi * i) / FRAME_COUNT
            frame = frame_from_phase(base, phase)
            import io

            buf = io.BytesIO()
            # baseline JPEG, 4:2:0 subsampling default — P4 HW decoder supports YUV420→RGB565
            frame.save(
                buf,
                format="JPEG",
                quality=args.quality,
                optimize=True,
                progressive=False,
                subsampling=2,  # 4:2:0
            )
            blob = buf.getvalue()
            if blob[:2] != b"\xff\xd8":
                print(f"ERROR: frame {i} not JPEG SOI", file=sys.stderr)
                return 1
            frames_jpeg.append(blob)
            csv_lines.append(f"{i},{payload_off},{len(blob)}")
            if mjpeg_f:
                mjpeg_f.write(blob)
            payload_off += len(blob)
            if (i + 1) % 50 == 0 or i == 0:
                print(f"  frame {i + 1}/{FRAME_COUNT}  jpeg={len(blob)} B")
    finally:
        if mjpeg_f:
            mjpeg_f.close()

    orsplash = out_dir / f"{stem}.orsplash"
    write_orsplash(frames_jpeg, orsplash)

    # Poster = first frame
    poster = out_dir / f"{stem}_Poster.jpg"
    poster.write_bytes(frames_jpeg[0])
    print(f"Wrote {poster}")

    csv_path = out_dir / f"{stem}_index.csv"
    csv_path.write_text("\n".join(csv_lines) + "\n", encoding="utf-8")
    print(f"Wrote {csv_path}")

    # Copy poster name expected by firmware fallback
    simple_poster = out_dir / "OrcSDR_Splash_Poster.jpg"
    simple_poster.write_bytes(frames_jpeg[0])
    print(f"Wrote {simple_poster}")

    avg = sum(len(b) for b in frames_jpeg) / len(frames_jpeg)
    print(f"Done. avg JPEG={avg:.0f} B  total payload={payload_off / (1024*1024):.2f} MiB")
    print("Copy the .orsplash to Tab5 microSD root (or /orcsdr/).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
