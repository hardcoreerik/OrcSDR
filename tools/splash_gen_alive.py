#!/usr/bin/env python3
"""
Generate *alive waveform* splash variants from the spectrum still.

  B_ae   — After-Effects-style: seamless Wave Warp + Turbulent Displace
  C_mesh — Blender-style: heightfield mesh with looping 3D displacement
  D_tviz — TheVisualizer-inspired: energy rolls + peak breathing on the surface

Writes 600 PNGs under out-dir, then optional --pack to .orsplash.
"""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageEnhance, ImageFilter


W, H, FPS, N = 1280, 720, 60, 600


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser()
    p.add_argument("--src", type=Path, required=True)
    p.add_argument("--variant", choices=["B_ae", "C_mesh", "D_tviz"], required=True)
    p.add_argument("--out-dir", type=Path, required=True)
    p.add_argument("--quality-preview", type=int, default=85)
    p.add_argument("--pack", type=Path, default=None, help="Also write .orsplash here")
    return p.parse_args()


def load_base(src: Path) -> tuple[np.ndarray, np.ndarray]:
    """RGB float32 HxWx3 0..1 and grayscale height map."""
    im = Image.open(src).convert("RGB")
    # Cover crop to 16:9 then resize
    tw, th = 16, 9
    scale = max(W / im.width, H / im.height)
    nw, nh = int(im.width * scale + 0.5), int(im.height * scale + 0.5)
    im = im.resize((nw, nh), Image.Resampling.LANCZOS)
    left, top = (nw - W) // 2, (nh - H) // 2
    im = im.crop((left, top, left + W, top + H))
    rgb = np.asarray(im).astype(np.float32) / 255.0
    # Height from cyan/luma emphasis
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    height = 0.15 * r + 0.35 * g + 0.50 * b
    height = (height - height.min()) / (height.max() - height.min() + 1e-6)
    return rgb, height


def sample_rgb(rgb: np.ndarray, xs: np.ndarray, ys: np.ndarray) -> np.ndarray:
    """Bilinear sample rgb at floating pixel coords."""
    h, w = rgb.shape[:2]
    xs = np.clip(xs, 0, w - 1.001)
    ys = np.clip(ys, 0, h - 1.001)
    x0 = np.floor(xs).astype(np.int32)
    y0 = np.floor(ys).astype(np.int32)
    x1 = np.minimum(x0 + 1, w - 1)
    y1 = np.minimum(y0 + 1, h - 1)
    fx = (xs - x0)[..., None]
    fy = (ys - y0)[..., None]
    c00 = rgb[y0, x0]
    c10 = rgb[y0, x1]
    c01 = rgb[y1, x0]
    c11 = rgb[y1, x1]
    return (c00 * (1 - fx) * (1 - fy) + c10 * fx * (1 - fy) + c01 * (1 - fx) * fy + c11 * fx * fy)


def gen_ae(rgb: np.ndarray, height: np.ndarray, phase: float) -> np.ndarray:
    """Wave Warp + Turbulent Displace (seamless)."""
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    # Multi-octave "turbulent" field, phase-locked for loop
    turb = (
        0.55 * np.sin(xx * 0.012 + phase)
        + 0.30 * np.sin(yy * 0.018 - phase * 1.3 + xx * 0.004)
        + 0.25 * np.sin((xx + yy) * 0.009 + phase * 2.0)
        + 0.15 * np.sin(xx * 0.031 - yy * 0.022 + phase * 0.7)
    )
    # Wave rolls along the mesh (X) and slight vertical energy
    wave_x = 14.0 * np.sin(yy * 0.020 + phase) + 8.0 * turb
    wave_y = 7.0 * np.sin(xx * 0.015 - phase * 1.1) + 5.0 * height * np.sin(phase * 2)
    # Extra peak breathing from height map
    breath = 6.0 * height * np.sin(phase + height * math.pi)
    xs = xx + wave_x + breath * 0.4
    ys = yy + wave_y + breath
    out = sample_rgb(rgb, xs, ys)
    # Glow pulse on peaks
    glow = 1.0 + 0.12 * height * (0.5 + 0.5 * np.sin(phase * 2 + xx * 0.01))
    out = np.clip(out * glow[..., None], 0, 1)
    return out


def gen_mesh(rgb: np.ndarray, height: np.ndarray, phase: float) -> np.ndarray:
    """
    Pseudo-3D mesh: project a displaced heightfield with looping noise,
    then texture-sample from original (Blender displace spirit).
    """
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    # Base UV in [-1,1]
    u = (xx / (W - 1)) * 2 - 1
    v = (yy / (H - 1)) * 2 - 1
    # Looping displacement on height
    noise = (
        0.35 * np.sin(u * 4.0 + phase)
        + 0.25 * np.sin(v * 5.0 - phase * 1.2)
        + 0.20 * np.sin((u + v) * 3.5 + phase * 2)
        + 0.15 * np.sin(u * 9.0 - v * 6.0 + phase * 0.5)
    )
    z = height * (1.0 + 0.45 * noise) * (0.85 + 0.15 * np.sin(phase))
    # Simple perspective: higher z shifts sample toward vanishing point (horizon)
    # Camera looks from bottom-front toward top-right of original art.
    persp = 0.22 * z
    # Parallax: peaks move more
    xs = xx + (u * 18.0 + 25.0 * noise) * (0.3 + z)
    ys = yy - z * 55.0 + 12.0 * np.sin(phase + u * 2) * height
    # Slight shear for 3D feel
    xs = xs + persp * (xx - W * 0.55) * 0.15
    ys = ys + persp * 20.0
    out = sample_rgb(rgb, xs, ys)
    # Specular ridge boost
    ridge = np.clip(z * 1.4, 0, 1)
    out = out * (0.88 + 0.22 * ridge[..., None])
    out = np.clip(out, 0, 1)
    # Soft vignette keep black void
    vign = np.clip(1.0 - 0.15 * ((u * 0.7) ** 2 + (v * 0.9) ** 2), 0.75, 1.0)
    return out * vign[..., None]


def gen_tviz(rgb: np.ndarray, height: np.ndarray, phase: float) -> np.ndarray:
    """
    TheVisualizer-inspired: cascading energy along spectrum, peak attacks,
    secondary ghost trail (like waterfall persistence).
    """
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    # Energy rolls from "hot" peaks toward quieter floor (down-right)
    cascade = np.sin(xx * 0.008 - yy * 0.014 + phase * 2.0)
    peak_drive = height * (0.55 + 0.45 * np.sin(phase * 3.0 + height * 6.0))
    # Lateral RF-like modulation (bin shimmer)
    bin_shim = np.sin(xx * 0.045 + phase * 4.0) * 0.5 + 0.5
    xs = xx + 10.0 * cascade * height + 6.0 * bin_shim * np.sin(phase)
    ys = yy + 18.0 * peak_drive * np.sin(phase + xx * 0.01) - 8.0 * cascade * height
    # Ghost: sample slightly upstream for trail
    xs2 = xs - 7.0 * cascade
    ys2 = ys - 5.0
    main = sample_rgb(rgb, xs, ys)
    ghost = sample_rgb(rgb, xs2, ys2)
    out = np.clip(main * 0.82 + ghost * 0.28, 0, 1)
    # Neon gain on moving peaks
    neon = 1.0 + 0.25 * peak_drive * (0.5 + 0.5 * np.sin(phase * 2))
    out[..., 1] = np.clip(out[..., 1] * neon, 0, 1)  # green/cyan
    out[..., 2] = np.clip(out[..., 2] * neon * 1.05, 0, 1)  # blue
    return out


def frame_to_image(arr: np.ndarray) -> Image.Image:
    u8 = (np.clip(arr, 0, 1) * 255.0 + 0.5).astype(np.uint8)
    im = Image.fromarray(u8, mode="RGB")
    # Light unsharp for peak crispness
    return im.filter(ImageFilter.UnsharpMask(radius=1.0, percent=70, threshold=2))


def main() -> int:
    args = parse_args()
    if not args.src.is_file():
        print(f"missing {args.src}", file=sys.stderr)
        return 1
    args.out_dir.mkdir(parents=True, exist_ok=True)
    print(f"Loading {args.src} variant={args.variant}")
    rgb, height = load_base(args.src)
    gen = {"B_ae": gen_ae, "C_mesh": gen_mesh, "D_tviz": gen_tviz}[args.variant]

    for i in range(N):
        phase = 2.0 * math.pi * i / N
        arr = gen(rgb, height, phase)
        im = frame_to_image(arr)
        path = args.out_dir / f"f_{i:04d}.png"
        im.save(path, compress_level=1)
        if (i + 1) % 50 == 0 or i == 0:
            print(f"  {args.variant} frame {i+1}/{N}")

    # Preview gif-ish: save first + mid frames as jpg
    Image.open(args.out_dir / "f_0000.png").save(args.out_dir / "preview_000.jpg", quality=90)
    Image.open(args.out_dir / "f_0300.png").save(args.out_dir / "preview_300.jpg", quality=90)
    print(f"Frames in {args.out_dir}")

    if args.pack:
        import subprocess

        cmd = [
            sys.executable,
            str(Path(__file__).resolve().parent / "splash_pack.py"),
            "--frames-dir",
            str(args.out_dir),
            "--out",
            str(args.pack),
            "--quality",
            "80",
        ]
        print(" ".join(cmd))
        subprocess.check_call(cmd)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
