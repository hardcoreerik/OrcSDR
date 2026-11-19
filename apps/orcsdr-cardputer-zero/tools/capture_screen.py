#!/usr/bin/env python3
"""Capture the visible Linux framebuffer without stopping OrcSDR."""
import argparse
from datetime import datetime
import struct
from pathlib import Path

from PIL import Image


def decode_frame(raw, width, height, stride, bpp, xoffset, yoffset, channels):
    if bpp not in (16, 32) or width <= 0 or height <= 0:
        raise ValueError("Expected a 16-bit or 32-bit framebuffer")
    if any(bits < 1 or bits > 8 or offset + bits > bpp or reversed_bits
           for offset, bits, reversed_bits in channels):
        raise ValueError("Unsupported framebuffer channel layout")
    pixel_bytes = bpp // 8
    if xoffset < 0 or yoffset < 0 or (xoffset + width) * pixel_bytes > stride:
        raise ValueError("Invalid framebuffer stride or offset")
    if len(raw) < (yoffset + height - 1) * stride + (xoffset + width) * pixel_bytes:
        raise ValueError("Truncated framebuffer")
    rgb = bytearray()
    for y in range(height):
        start = (y + yoffset) * stride + xoffset * pixel_bytes
        for x in range(width):
            at = start + x * pixel_bytes
            value = int.from_bytes(raw[at:at + pixel_bytes], "little")
            for offset, bits, _ in channels:
                maximum = (1 << bits) - 1
                rgb.append(((value >> offset) & maximum) * 255 // maximum)
    return Image.frombytes("RGB", (width, height), bytes(rgb))


def main():
    import fcntl

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", nargs="?", type=Path,
                        default=Path.home() / "Pictures" / ("orcsdr-" + datetime.now().strftime("%Y%m%d-%H%M%S-%f") + ".png"))
    parser.add_argument("--device", default="/dev/fb0")
    args = parser.parse_args()
    info = bytearray(160)
    with open(args.device, "rb", buffering=0) as framebuffer:
        fcntl.ioctl(framebuffer, 0x4600, info)  # FBIOGET_VSCREENINFO
        fields = struct.unpack_from("=20I", info)
        width, height, _, virtual_height, xoffset, yoffset, bpp = fields[:7]
        stride = int((Path("/sys/class/graphics") / Path(args.device).resolve().name / "stride").read_text())
        if height <= 0 or yoffset + height > virtual_height or stride <= 0 or stride * virtual_height > 64 * 1024 * 1024:
            raise ValueError("Invalid or oversized framebuffer")
        raw = framebuffer.read(stride * (yoffset + height))
    image = decode_frame(raw, width, height, stride, bpp, xoffset, yoffset,
                         (fields[8:11], fields[11:14], fields[14:17]))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # Exclusive creation preserves earlier captures even with an explicit filename.
    with args.output.open("xb") as output:
        image.save(output, format="PNG")
    print(f"Saved {width}x{height} PNG: {args.output}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as error:
        raise SystemExit(f"Screenshot failed: {error}")
