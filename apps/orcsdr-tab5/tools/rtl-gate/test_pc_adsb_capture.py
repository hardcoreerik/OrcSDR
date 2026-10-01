#!/usr/bin/env python3
"""Self-test for pc_adsb_capture.py: CRC-24 and the frame filter, using real published ADS-B example frames."""

import shutil
import tempfile
import unittest
from pathlib import Path

import numpy as np

from pc_adsb_capture import crc24_remainder, decode_iq, ensure_decoder, parse_frame

# Well-known DF17 example frames (ICAO 4840D6 identification, 40621D position, 4840D6 velocity).
VALID = [
    ("8D4840D6202CC371C32CE0576098", "4840D6"),
    ("8D40621D58C382D690C8AC2863A7", "40621D"),
    ("8D485020994409940838175B284F", "485020"),
]


class Crc24Tests(unittest.TestCase):
    def test_valid_frames_have_zero_remainder(self):
        for hexa, icao in VALID:
            self.assertEqual(crc24_remainder(bytes.fromhex(hexa)), 0, hexa)
            self.assertEqual(parse_frame(f"*{hexa.lower()};"), (icao, 17, hexa))

    def test_single_bit_error_is_rejected(self):
        raw = bytearray(bytes.fromhex(VALID[0][0]))
        raw[5] ^= 0x04
        self.assertNotEqual(crc24_remainder(bytes(raw)), 0)
        self.assertIsNone(parse_frame(f"*{raw.hex()};"))

    def test_non_extended_squitter_and_junk_are_ignored(self):
        self.assertIsNone(parse_frame("*5d4840d6aabbcc;"))            # short frame
        self.assertIsNone(parse_frame("*ffeec8c80a0713de3c05f3cbd9ba;"))  # DF31 garbage
        self.assertIsNone(parse_frame("Found 1 device(s):"))
        self.assertIsNone(parse_frame("*8d4840d6202cc371c32ce05760"))     # no terminator


RATE = 2_048_000  # the decoder's bit timing is built for exactly 2.048 MS/s


def synth_iq(frames_hex, noise_sd=3.0, amplitude=70.0, gap=1500, seed=3):
    """CU8 IQ at 2.048 MS/s: each frame is the 8 us preamble then 112 PPM bits (1 us each), over noise."""
    rng = np.random.default_rng(seed)
    mags = [np.abs(rng.normal(0, noise_sd, gap))]
    us = RATE / 1e6  # samples per microsecond
    for hexa in frames_hex:
        bits = np.unpackbits(np.frombuffer(bytes.fromhex(hexa), dtype=np.uint8))
        frame = np.zeros(int((8 + len(bits) + 8) * us))
        t = np.arange(frame.size) / us  # microseconds from the preamble start
        for start in (0.0, 1.0, 3.5, 4.5):  # the four 0.5 us preamble pulses
            frame[(t >= start) & (t < start + 0.5)] = amplitude
        for i, bit in enumerate(bits):
            first = 8.0 + i  # a 1 is high in the first half of its microsecond, a 0 in the second
            lo = first if bit else first + 0.5
            frame[(t >= lo) & (t < lo + 0.5)] = amplitude
        mags.append(frame + np.abs(rng.normal(0, noise_sd, frame.size)))
        mags.append(np.abs(rng.normal(0, noise_sd, gap)))
    iq = np.zeros(2 * sum(m.size for m in mags), dtype=np.uint8)
    flat = np.concatenate(mags)
    iq[0::2] = np.clip(np.round(127 + flat), 0, 255)
    iq[1::2] = 127
    return iq


@unittest.skipUnless(shutil.which("wsl.exe") or shutil.which("wsl"), "needs WSL to build the host decoder")
class DecoderOnIqTests(unittest.TestCase):
    def test_known_frame_is_decoded_from_synthetic_iq(self):
        # Tests the plumbing (build, run, parse), not the decoder: only the first example frame decodes from
        # these idealised rectangular pulses (the decoder's bit-timing window is calibrated on real, band-limited
        # signals), so the others are deliberately not asserted.
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "synthetic.cu8"
            synth_iq([VALID[0][0]]).tofile(path)
            ensure_decoder()
            rows = [r.split(",") for r in decode_iq(path)]
            self.assertEqual([r[1] for r in rows], ["4840D6"], rows)
            self.assertEqual(rows[0][2], "17")

    def test_noise_only_gives_no_frames(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "noise.cu8"
            synth_iq([]).tofile(path)
            ensure_decoder()
            self.assertEqual(decode_iq(path), [])


if __name__ == "__main__":
    unittest.main()
