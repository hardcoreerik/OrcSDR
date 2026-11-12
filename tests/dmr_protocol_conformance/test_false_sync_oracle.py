"""ETSI Table 9.2 DATA/VOICE sync decision geometry, independent of OrcSDR.

This is a classification *oracle*, not a DMR packet validator: neither sync
nor Golay nor CACH authenticates an identity. See the linked C++ tests for
BPTC+RS/CRC integrity checks against the production decoder.
"""
from __future__ import annotations

import itertools
import random
import unittest

DATA = {
    "bs": 0xDFF57D75DF5D,
    "ms": 0xD5D7F77FD757,
    "direct_ts1": 0xF7FDD5DDFD55,
    "direct_ts2": 0xD7557F5FF7F5,
}
VOICE = {
    "bs": 0x755FD7DF75F7,
    "ms": 0x7F7D5DD57DFD,
    "direct_ts1": 0x5D577F7757FF,
    "direct_ts2": 0x7DFFD5F55D5F,
}
SEED = 0xD1F20261111
MASK48 = (1 << 48) - 1


def hamming(a: int, b: int) -> int:
    return (a ^ b).bit_count()


def decision(word: int) -> tuple[str, str, int] | None:
    """Independent acceptability of a sync *candidate*, excluding integrity."""
    exact = [(k, name) for k, mapping in (("data", DATA), ("voice", VOICE))
             for name, ref in mapping.items() if ref == word]
    if len(exact) == 1:
        return (exact[0][0], exact[0][1], 0)
    if exact:
        return None
    fuzzy = [name for name, ref in DATA.items() if hamming(word, ref) == 1]
    return ("data", fuzzy[0], 1) if len(fuzzy) == 1 else None


class IndependentSyncGeometry(unittest.TestCase):
    def test_eight_distinct_printed_etsi_words(self):
        self.assertEqual(len(set(DATA.values()) | set(VOICE.values())), 8)
        for mode in DATA:
            # Table 9.2: each voice/data pair is a symbol-wise complement.
            self.assertEqual(DATA[mode] ^ VOICE[mode], 0xAAAAAAAAAAAA)

    def test_every_single_bit_candidate_and_uniqueness(self):
        seen = set()
        for name, word in DATA.items():
            self.assertEqual(decision(word), ("data", name, 0))
            for bit in range(48):
                damaged = word ^ (1 << bit)
                self.assertNotIn(damaged, seen)
                seen.add(damaged)
                self.assertEqual(decision(damaged), ("data", name, 1))
        self.assertEqual(len(seen), 4 * 48)
        for name, word in VOICE.items():
            self.assertEqual(decision(word), ("voice", name, 0))
            for bit in range(48):
                self.assertIsNone(decision(word ^ (1 << bit)))

    def test_all_two_and_three_bit_corruptions_rejected(self):
        examined = 0
        for word in DATA.values():
            for a, b in itertools.combinations(range(48), 2):
                self.assertIsNone(decision(word ^ (1 << a) ^ (1 << b)))
                examined += 1
            for a, b, c in itertools.combinations(range(48), 3):
                self.assertIsNone(decision(word ^ (1 << a) ^ (1 << b) ^ (1 << c)))
                examined += 1
        self.assertEqual(examined, 4 * (1128 + 17296))

    def test_multi_bit_distance_not_assumed_for_all_patterns(self):
        # The policy is *distance one*, not a claim that every distance >=4
        # cannot be one bit from some OTHER accepted DATA sync. Check geometry.
        for a, b in itertools.combinations(DATA.values(), 2):
            self.assertGreaterEqual(hamming(a, b), 8)
        for value in DATA.values():
            for number in (4, 8, 16, 24, 36, 48):
                mask = (1 << number) - 1
                self.assertIsNone(decision(value ^ mask))

    def test_random_48bit_candidate_rate(self):
        rng = random.Random(SEED)
        trials = 250_000
        candidates = 0
        verified_identities = 0  # This oracle never decodes payload/identity.
        for _ in range(trials):
            candidates += decision(rng.getrandbits(48) & MASK48) is not None
        # Uniform 48-bit chance of exact or 1-bit nearby any of 4 data
        # words is (8 + 4*48)/2**48, even before integrity gating.
        p = (8 + 4 * 48) / (2 ** 48)
        self.assertLess(p, 1e-12)
        self.assertEqual(candidates, 0)
        self.assertEqual(verified_identities, 0)
        print(f"GEOMETRY seed=0x{SEED:x} random_48bit={trials} "
              f"candidates={candidates} theoretical_per_window={p:.3e}")


if __name__ == "__main__":
    unittest.main(verbosity=2)
