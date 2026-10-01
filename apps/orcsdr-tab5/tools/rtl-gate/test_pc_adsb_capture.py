#!/usr/bin/env python3
"""Self-test for pc_adsb_capture.py: CRC-24 and the frame filter, using real published ADS-B example frames."""

import unittest

from pc_adsb_capture import crc24_remainder, parse_frame

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


if __name__ == "__main__":
    unittest.main()
