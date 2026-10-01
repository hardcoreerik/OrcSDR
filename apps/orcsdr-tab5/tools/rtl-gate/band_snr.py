#!/usr/bin/env python3
"""Summarise one raw CU8 IQ capture as a few numbers that compare across builds and dongles.

The IQ is averaged into one power spectrum (2048-point Hann FFT). Reported:
  noise_dbfs        median bin power, the noise floor
  peak_over_noise   strongest bin above that floor (dB), ignoring the DC spike and the band edges
  peak_offset_khz   where that strongest bin sits relative to the tuned frequency
  strong_bins       bins more than 10 dB above the floor (a rough "how many signals" count)
  clipping_pct      samples at 0 or 255

The numbers are only comparable at the same gain, bandwidth and antenna. Prints one JSON object.

    python band_snr.py capture.u8 --rate 2400000
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np


def summarise(path: Path, rate: int, fft_size: int = 2048) -> dict[str, float]:
    raw = np.fromfile(path, dtype=np.uint8)
    if raw.size < fft_size * 4 or raw.size % 2:
        raise ValueError("capture too short or odd length")
    pairs = raw.reshape(-1, 2)
    clipping = float(np.mean((pairs == 0) | (pairs == 255)) * 100.0)
    iq = (pairs[:, 0].astype(np.float64) - 127.5) + 1j * (pairs[:, 1].astype(np.float64) - 127.5)
    frames = iq[: (iq.size // fft_size) * fft_size].reshape(-1, fft_size)
    window = np.hanning(fft_size)
    spectrum = np.mean(np.abs(np.fft.fftshift(np.fft.fft(frames * window, axis=1), axes=1)) ** 2, axis=0)
    full_scale = (127.5 * np.sum(window)) ** 2
    db = 10.0 * np.log10(spectrum / full_scale + 1e-30)
    noise = float(np.median(db))
    centre, edge = fft_size // 2, int(fft_size * 0.03)
    usable = np.ones(fft_size, dtype=bool)
    usable[centre - 6 : centre + 7] = False   # DC spike
    usable[:edge] = False
    usable[-edge:] = False
    peak_bin = int(np.argmax(np.where(usable, db, -1e9)))
    return {
        "noise_dbfs": round(noise, 2),
        "peak_over_noise_db": round(float(db[peak_bin] - noise), 2),
        "peak_offset_khz": round((peak_bin - centre) * rate / fft_size / 1000.0, 1),
        "strong_bins": int(np.sum(usable & (db > noise + 10.0))),
        "clipping_pct": round(clipping, 4),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--rate", type=int, default=2_400_000)
    args = parser.parse_args()
    print(json.dumps(summarise(args.capture, args.rate)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
