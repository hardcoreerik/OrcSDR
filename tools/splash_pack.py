#!/usr/bin/env python3
"""Pack a video or frame folder into an OrcSDR ORSPLASH v1 asset."""

from __future__ import annotations

import argparse
import io
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

W, H = 1280, 720
DEFAULT_FPS, DEFAULT_FRAME_COUNT = 60, 600
HEADER_SIZE = 32
MAGIC = b"ORCSPLSH"
FFMPEG = "ffmpeg"


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser()
    p.add_argument("--video", type=Path, help="Input video (resampled to --frame-count frames)")
    p.add_argument("--frames-dir", type=Path, help="Directory of images sorted by name")
    p.add_argument("--out", type=Path, required=True, help="Output .orsplash path")
    p.add_argument("--quality", type=int, default=80)
    p.add_argument("--fps", type=int, default=DEFAULT_FPS)
    p.add_argument("--frame-count", type=int, default=DEFAULT_FRAME_COUNT)
    p.add_argument(
        "--tab5-native",
        action="store_true",
        help="Rotate frames clockwise to the Tab5 native 720x1280 framebuffer",
    )
    p.add_argument("--poster", type=Path, default=None)
    p.add_argument("--keep-work", action="store_true")
    return p.parse_args()


def extract_frames(video: Path, out_dir: Path, fps: int, frame_count: int) -> list[Path]:
    out_dir.mkdir(parents=True, exist_ok=True)
    pattern = str(out_dir / "f_%04d.png")
    # Scale + fps, take the requested frame count (loop-pad short inputs).
    cmd = [
        FFMPEG,
        "-y",
        "-stream_loop",
        "2",
        "-i",
        str(video),
        "-vf",
        f"scale={W}:{H}:force_original_aspect_ratio=increase,crop={W}:{H},fps={fps}",
        "-frames:v",
        str(frame_count),
        pattern,
    ]
    print(" ".join(cmd))
    subprocess.check_call(cmd)
    frames = sorted(out_dir.glob("f_*.png"))
    if len(frames) < frame_count:
        raise SystemExit(f"Only got {len(frames)} frames from video")
    return frames[:frame_count]


def load_frame_paths(frames_dir: Path, frame_count: int) -> list[Path]:
    exts = {".png", ".jpg", ".jpeg", ".webp", ".bmp"}
    frames = sorted(p for p in frames_dir.iterdir() if p.suffix.lower() in exts)
    if not frames:
        raise SystemExit(f"No frames in {frames_dir}")
    if len(frames) < frame_count:
        # Pad by looping
        padded = []
        while len(padded) < frame_count:
            padded.extend(frames)
        frames = padded[:frame_count]
    elif len(frames) > frame_count:
        # Uniform sample
        idxs = [i * len(frames) // frame_count for i in range(frame_count)]
        frames = [frames[i] for i in idxs]
    return frames


def to_jpeg(im: Image.Image, quality: int, tab5_native: bool = False) -> bytes:
    im = im.convert("RGB")
    if im.size != (W, H):
        im = im.resize((W, H), Image.Resampling.LANCZOS)
    if tab5_native:
        im = im.transpose(Image.Transpose.ROTATE_270)
    buf = io.BytesIO()
    im.save(buf, format="JPEG", quality=quality, optimize=True, progressive=False, subsampling=2)
    return buf.getvalue()


def write_orsplash(
    blobs: list[bytes], out_path: Path, fps: int, width: int, height: int
) -> None:
    frame_count = len(blobs)
    index_offset = HEADER_SIZE
    index_bytes = frame_count * 8
    data_offset = index_offset + index_bytes
    payload = b"".join(blobs)
    header = struct.pack(
        "<8sHHHHHHIII",
        MAGIC,
        1,
        width,
        height,
        fps,
        frame_count,
        0x0001,
        index_offset,
        data_offset,
        len(payload),
    )
    index = bytearray()
    off = 0
    for b in blobs:
        index += struct.pack("<II", off, len(b))
        off += len(b)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("wb") as f:
        f.write(header)
        f.write(index)
        f.write(payload)
    print(f"Wrote {out_path} ({out_path.stat().st_size / (1024*1024):.2f} MiB) frames={len(blobs)}")


def main() -> int:
    args = parse_args()
    if not 1 <= args.fps <= 60 or not 1 <= args.frame_count <= 65535:
        print("fps must be 1..60 and frame-count must be 1..65535", file=sys.stderr)
        return 1
    work = Path(tempfile.mkdtemp(prefix="orsplash_"))
    try:
        if args.video:
            frame_paths = extract_frames(args.video, work / "frames", args.fps, args.frame_count)
        elif args.frames_dir:
            frame_paths = load_frame_paths(args.frames_dir, args.frame_count)
        else:
            print("Need --video or --frames-dir", file=sys.stderr)
            return 1

        blobs: list[bytes] = []
        for i, p in enumerate(frame_paths):
            im = Image.open(p)
            blobs.append(to_jpeg(im, args.quality, args.tab5_native))
            if (i + 1) % 50 == 0 or i == 0:
                print(f"  encode {i+1}/{args.frame_count} {len(blobs[-1])} B")

        width, height = ((H, W) if args.tab5_native else (W, H))
        write_orsplash(blobs, args.out, args.fps, width, height)
        poster = args.poster or args.out.with_name(args.out.stem + "_Poster.jpg")
        poster.write_bytes(to_jpeg(Image.open(frame_paths[0]), args.quality))
        print(f"Wrote {poster}")
    finally:
        if not args.keep_work:
            import shutil

            shutil.rmtree(work, ignore_errors=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
