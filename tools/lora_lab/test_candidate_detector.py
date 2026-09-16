import io
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

import numpy as np

from candidate_detector import _resample, chirp_metrics, downchirp, impair_awgn, main


class CandidateDetectorTests(unittest.TestCase):
    @patch("candidate_detector.capture_metrics", return_value={})
    @patch("candidate_detector.impairment_metrics", return_value={})
    def test_cli_passes_target_rms_to_impairment(self, impairment, _capture):
        arguments = [
            "candidate_detector.py", "--snr-db", "-25", "--target-rms", "0.02", "capture.orciq"
        ]

        with patch.object(sys, "argv", arguments), patch("sys.stdout", new=io.StringIO()):
            self.assertEqual(main(), 0)

        impairment.assert_called_once_with(Path("capture.orciq"), -25.0, 90210, 0.02)

    def test_awgn_impairment_is_seeded_and_meets_requested_snr(self):
        signal = np.ones(200_000, dtype=np.complex64)

        first, details = impair_awgn(signal, 1.0, 5.0, 90210)
        second, _ = impair_awgn(signal, 1.0, 5.0, 90210)

        scaled = signal * details["scale"]
        measured_noise_rms = np.sqrt(np.mean(np.abs(first - scaled) ** 2))
        measured_snr = 20 * np.log10(0.05 / measured_noise_rms)
        np.testing.assert_array_equal(first, second)
        self.assertAlmostEqual(details["scale"], 0.05)
        self.assertAlmostEqual(measured_snr, 5.0, delta=0.1)

    def test_resample_position_does_not_overflow_windows_numpy_integer(self):
        signal = np.arange(20_000, dtype=np.float32).astype(np.complex64)

        resampled = _resample(signal, 960_000, 500_000)

        self.assertAlmostEqual(resampled[5_000].real, 9_600.0)

    def test_repeated_upchirps_separate_from_seeded_noise(self):
        sf = 11
        bandwidth = 250_000
        rate = 500_000
        reference = downchirp(sf, bandwidth, rate)
        upchirps = np.tile(np.conj(reference), 16)
        noise = (
            np.random.default_rng(90210).normal(0, 0.1, upchirps.size)
            + 1j * np.random.default_rng(90211).normal(0, 0.1, upchirps.size)
        )

        positive = chirp_metrics(upchirps, sf, bandwidth, rate)
        negative = chirp_metrics(noise, sf, bandwidth, rate)

        self.assertGreaterEqual(positive["max_consecutive"], 15)
        self.assertGreater(positive["peak_to_median"], 50)
        self.assertLessEqual(negative["max_consecutive"], 3)


if __name__ == "__main__":
    unittest.main()
