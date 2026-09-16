import sys
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).parent))
import analyze_lora_recovery_oracle


class RecoveryOracleTests(unittest.TestCase):
    def test_one_symbol_oracle_identifies_the_only_identity_restoring_candidate(self):
        report = {
            "host_packet_id": 42,
            "traces": [
                "RTL_LORA_NATIVE_ALTERNATES sequence=1 count=2 "
                "values=8:10:1.100:110.0:100.0,9:20:1.020:102.0:100.0",
                "RTL_LORA_NATIVE_SYMBOLS sequence=1 count=10 "
                "values=0,0,0,0,0,0,0,0,11,21",
            ],
        }

        def decode_candidate(symbols):
            return 42 if symbols[9] == 20 else None

        result = analyze_lora_recovery_oracle._analyze_report(
            report, decode_candidate, bins=2048
        )

        self.assertEqual(result["eligible_candidates"], 2)
        self.assertEqual(result["successful_candidates"], 1)
        self.assertEqual(result["winner"]["symbol_index"], 9)
        self.assertEqual(result["winner"]["ratio_rank"], 1)
        self.assertEqual(result["winner"]["margin_rank"], 1)
        self.assertEqual(result["winner"]["packet_id"], 42)

    def test_missing_expected_identity_does_not_match_failed_candidates(self):
        report = {
            "host_packet_id": None,
            "traces": [
                "RTL_LORA_NATIVE_ALTERNATES sequence=1 count=1 "
                "values=8:10:1.100:110.0:100.0",
                "RTL_LORA_NATIVE_SYMBOLS sequence=1 count=9 "
                "values=0,0,0,0,0,0,0,0,11",
            ],
        }

        result = analyze_lora_recovery_oracle._analyze_report(
            report, lambda symbols: None, bins=2048
        )

        self.assertEqual(result["successful_candidates"], 0)
        self.assertFalse(result["candidates"][0]["identity_match"])


if __name__ == "__main__":
    unittest.main()
