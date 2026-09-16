import sys
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).parent))
import run_lora_native_matrix


class MatrixRowTests(unittest.TestCase):
    def test_resume_key_includes_target_rms(self):
        self.assertNotEqual(
            run_lora_native_matrix._matrix_key("capture", 1, -23.0, 0.02),
            run_lora_native_matrix._matrix_key("capture", 1, -23.0, 0.05),
        )

    def test_host_pass_native_fail_without_all_correct_alternates_is_class_b(self):
        capture = {
            "capture_id": "orciq-example",
            "path": "mla30/example.orciq",
            "expected": {"packet_id": 42, "plaintext": "matrix"},
        }
        report = {
            "host_decode": True,
            "host_packet_id": 42,
            "done": (
                "RTL_LORA_NATIVE_DONE packets=0 preambles=1 header_failures=0 "
                "crc_ok=0 crc_failures=1 elapsed_ms=13000"
            ),
            "profile": (
                "RTL_LORA_NATIVE_PROFILE cfo_hypotheses=1 clock_hypotheses=1 "
                "timing_offsets=1 fft_calls=480 recovery_attempted=1 "
                "recovery_symbols_considered=110 recovery_candidates_tested=1 "
                "recovery_success=0 recovery_exhausted=1"
            ),
            "packets": [],
            "symbol_difference": {"different": 2, "indices": [10, 11]},
            "alternate_coverage": {
                "errors": 2, "error_indices": [10, 11],
                "covered": 1, "covered_indices": [10],
            },
            "traces": [
                "RTL_LORA_NATIVE_ALTERNATES sequence=1 count=2 "
                "values=10:100:1.025,11:200:1.400",
                "RTL_LORA_NATIVE_SYMBOLS sequence=1 count=12 "
                "values=0,0,0,0,0,0,0,0,0,0,101,202",
            ],
            "memory": [],
        }

        row = run_lora_native_matrix._build_row(capture, -23, 90500, report)

        self.assertEqual(row["classification"], "B")
        self.assertTrue(row["host_exact"])
        self.assertFalse(row["native_exact"])
        self.assertEqual(row["primary_error_indices"], [10, 11])
        self.assertEqual(row["recovered"], [
            {"index": 10, "symbol": 100, "ratio": 1.025},
        ])
        self.assertEqual(row["candidate_changed_indices"], [10])
        self.assertEqual(row["candidate_collateral_indices"], [])
        self.assertEqual(row["recovery_candidates_tested"], 1)
        self.assertTrue(row["recovery_exhausted"])
        self.assertEqual(row["expected_packet_id"], 42)
        self.assertIsNone(row["final_packet_id"])


if __name__ == "__main__":
    unittest.main()
