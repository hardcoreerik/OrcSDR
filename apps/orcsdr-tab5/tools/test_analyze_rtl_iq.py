import tempfile
import unittest
from pathlib import Path

import numpy as np

from analyze_rtl_iq import analyze_capture, ascii_spectrum


def write_cu8(path: Path, samples: np.ndarray) -> None:
    peak = max(float(np.max(np.abs(samples))), 1.0)
    scaled = samples * (120.0 / peak)
    iq = np.column_stack((scaled.real, scaled.imag)) + 127.5
    np.clip(np.rint(iq), 0, 255).astype(np.uint8).tofile(path)


class AnalyzeRtlIqTests(unittest.TestCase):
    def test_terminal_spectrum_is_ascii_safe(self) -> None:
        summary = ascii_spectrum(np.linspace(-100.0, -20.0, 2048))
        self.assertEqual(len(summary), 80)
        self.assertTrue(summary.isascii())

    def test_balanced_noise_has_comparable_halves(self) -> None:
        rng = np.random.default_rng(20260915)
        samples = rng.normal(size=32768) + 1j * rng.normal(size=32768)
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "balanced.cu8"
            write_cu8(path, samples)
            metrics = analyze_capture(path, 2_400_000, fft_size=2048)
        self.assertLess(abs(metrics["half_power_delta_db"]), 1.5)

    def test_positive_only_noise_reports_large_signed_delta(self) -> None:
        rng = np.random.default_rng(991)
        sample_index = np.arange(32768)
        samples = sum(
            np.exp(1j * (2 * np.pi * bin_index * sample_index / 32768 + phase))
            for bin_index, phase in zip(
                np.linspace(512, 14000, 64, dtype=int),
                rng.uniform(0, 2 * np.pi, 64),
            )
        )
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "one_sided.cu8"
            write_cu8(path, samples)
            metrics = analyze_capture(path, 2_400_000, fft_size=2048)
        self.assertGreater(metrics["half_power_delta_db"], 12.0)


if __name__ == "__main__":
    unittest.main()
