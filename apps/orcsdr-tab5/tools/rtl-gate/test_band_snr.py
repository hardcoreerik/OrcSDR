#!/usr/bin/env python3
"""Self-test for band_snr.py: a known tone in noise must be found at the right offset and level."""

import tempfile
import unittest
from pathlib import Path

import numpy as np

from band_snr import summarise

RATE = 2_400_000


def write_cu8(path: Path, tone_hz: float | None, tone_amp: float, noise_sd: float, seconds: float = 0.2) -> None:
    rng = np.random.default_rng(7)
    n = int(RATE * seconds)
    t = np.arange(n) / RATE
    iq = (rng.normal(0, noise_sd, n) + 1j * rng.normal(0, noise_sd, n)).astype(np.complex128)
    if tone_hz is not None:
        iq += tone_amp * np.exp(2j * np.pi * tone_hz * t)
    out = np.empty(2 * n, dtype=np.uint8)
    out[0::2] = np.clip(np.round(iq.real + 127.5), 0, 255)
    out[1::2] = np.clip(np.round(iq.imag + 127.5), 0, 255)
    out.tofile(path)


class BandSnrTests(unittest.TestCase):
    def test_tone_offset_and_level(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "tone.u8"
            write_cu8(p, tone_hz=300_000, tone_amp=30.0, noise_sd=3.0)
            r = summarise(p, RATE)
            self.assertAlmostEqual(r["peak_offset_khz"], 300.0, delta=2.0)
            self.assertGreater(r["peak_over_noise_db"], 25.0)
            self.assertGreaterEqual(r["strong_bins"], 1)
            self.assertEqual(r["clipping_pct"], 0.0)

    def test_noise_only_has_small_peak(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "noise.u8"
            write_cu8(p, tone_hz=None, tone_amp=0.0, noise_sd=3.0)
            r = summarise(p, RATE)
            self.assertLess(r["peak_over_noise_db"], 12.0)
            self.assertEqual(r["strong_bins"], 0)

    def test_louder_noise_raises_the_floor(self):
        with tempfile.TemporaryDirectory() as d:
            quiet, loud = Path(d) / "q.u8", Path(d) / "l.u8"
            write_cu8(quiet, None, 0.0, 2.0)
            write_cu8(loud, None, 0.0, 8.0)
            self.assertGreater(summarise(loud, RATE)["noise_dbfs"], summarise(quiet, RATE)["noise_dbfs"] + 9.0)

    def test_clipping_is_counted(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "clip.u8"
            write_cu8(p, tone_hz=100_000, tone_amp=400.0, noise_sd=3.0)
            self.assertGreater(summarise(p, RATE)["clipping_pct"], 10.0)


if __name__ == "__main__":
    unittest.main()
