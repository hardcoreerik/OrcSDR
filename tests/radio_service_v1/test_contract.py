"""Radio Service v1 JSON schema, fixture, negative, and pure adapter tests."""
import copy
import json
import sys
import unittest
from pathlib import Path

from jsonschema import Draft202012Validator

ROOT = Path(__file__).resolve().parents[2]
DIR = ROOT / "contracts" / "radio_service" / "v1"
sys.path.insert(0, str(DIR))
from compat import (check_semantics, complete_tune, envelope, headless_health,
                    headless_state, mobile_state, preflight_tune)  # noqa: E402


class RadioServiceContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.schema = json.loads((DIR / "schema.json").read_text(encoding="utf-8"))
        Draft202012Validator.check_schema(cls.schema)
        cls.validator = Draft202012Validator(cls.schema)
        cls.examples = {
            f.name: json.loads(f.read_text(encoding="utf-8"))
            for f in sorted((DIR / "examples").glob("*.json"))
        }

    def example(self, name):
        return copy.deepcopy(self.examples[name])

    def assert_invalid(self, obj):
        self.assertFalse(self.validator.is_valid(obj), f"incorrectly accepted: {obj!r}")

    def test_schema_is_valid_and_all_golden_examples_pass(self):
        self.assertEqual(14, len(self.examples))
        for filename, obj in self.examples.items():
            with self.subTest(filename=filename):
                self.validator.validate(obj)
                check_semantics(obj)

    def test_incompatible_hello_has_no_selected_version_or_scopes(self):
        hello = self.example("01-hello.json")
        hello["payload"]["supported_versions"] = [{"major": 2, "minor": 0}]
        self.validator.validate(hello)  # negotiation can reject a v2-only offer
        result = self.example("14-incompatible-hello.json")
        self.validator.validate(result)
        result["payload"]["selected_version"] = {"major": 1, "minor": 0}
        self.assert_invalid(result)
        result = self.example("14-incompatible-hello.json")
        result["payload"]["granted_scopes"] = ["tune"]
        self.assert_invalid(result)

    def test_iq_requires_attachment_and_live_session_fence(self):
        obj = self.example("09-iq-block.json")
        obj["attachment_id"] = None
        self.assert_invalid(obj)
        obj = self.example("09-iq-block.json")
        obj["generation"] = 0
        self.assert_invalid(obj)

    def test_unknown_top_level_field_rejected(self):
        obj = self.example("09-iq-block.json")
        obj["unexpected"] = True
        self.assert_invalid(obj)

    def test_wrong_major_rejected(self):
        obj = self.example("01-hello.json")
        obj["version"]["major"] = 2
        self.assert_invalid(obj)

    def test_wrong_command_kind_or_operation_rejected(self):
        obj = self.example("05-tune-command.json")
        obj["payload"]["operation"] = "secret_action"
        self.assert_invalid(obj)

    def test_tune_missing_requested_frequency_rejected(self):
        obj = self.example("05-tune-command.json")
        obj["payload"]["requested"] = {"gain_tenth_db": 100}
        self.assert_invalid(obj)

    def test_bad_frequency_and_bool_rejected(self):
        for hz in [0, -1, True, "101700000"]:
            obj = self.example("05-tune-command.json")
            obj["payload"]["requested"]["frequency_hz"] = hz
            self.assert_invalid(obj)

    def test_pending_never_claims_applied_or_ack(self):
        obj = self.example("06-tune-pending.json")
        obj["payload"]["backend_ack"] = True
        self.assert_invalid(obj)
        obj = self.example("06-tune-pending.json")
        obj["payload"]["applied"] = self.example("07-tune-applied.json")["payload"]["applied"]
        self.assert_invalid(obj)

    def test_applied_requires_ack_source(self):
        obj = self.example("07-tune-applied.json")
        obj["payload"]["ack_source"] = None
        self.assert_invalid(obj)

    def test_failed_requires_error(self):
        obj = self.example("08-tune-stale.json")
        obj["payload"]["error"] = None
        self.assert_invalid(obj)

    def test_unknown_utc_must_not_be_fabricated(self):
        obj = self.example("12-decoder-event.json")
        obj["time"]["utc_unix_ms"] = 1794000000000
        self.assert_invalid(obj)
        obj = self.example("12-decoder-event.json")
        obj["time"].update(utc_valid=True, utc_unix_ms=1794000000000,
                           utc_uncertainty_ms=None)
        self.assert_invalid(obj)

    def test_unknown_health_is_nullable_not_zero(self):
        obj = self.example("13-receiver-health.json")
        self.assertIsNone(obj["payload"]["dropped_complex_samples"])
        self.validator.validate(obj)
        obj["payload"]["dropped_complex_samples"] = -1
        self.assert_invalid(obj)

    def test_iq_geometry_and_spectrum_bin_count(self):
        obj = self.example("09-iq-block.json")
        obj["payload"]["payload_bytes"] = 123
        self.validator.validate(obj)
        with self.assertRaises(ValueError):
            check_semantics(obj)
        obj = self.example("10-spectrum-frame.json")
        obj["payload"]["bin_count"] += 1
        self.validator.validate(obj)
        with self.assertRaises(ValueError):
            check_semantics(obj)

    def test_audio_geometry(self):
        obj = self.example("11-audio-frame.json")
        obj["payload"]["payload_bytes"] = 100
        with self.assertRaises(ValueError):
            check_semantics(obj)

    def test_inverted_rf_and_rate_ranges(self):
        obj = self.example("03-capabilities.json")
        obj["payload"]["receiver"]["frequency_ranges"][0]["min_hz"] = 1800000000
        with self.assertRaises(ValueError):
            check_semantics(obj)
        obj = self.example("03-capabilities.json")
        obj["payload"]["receiver"]["sample_rate_ranges_sps"] = [{"min_sps": 300, "max_sps": 200}]
        with self.assertRaises(ValueError):
            check_semantics(obj)

    def test_tx_not_inferred(self):
        obj = self.example("03-capabilities.json")
        obj["payload"]["receiver"]["tx"]["modes"] = ["lora"]
        with self.assertRaises(ValueError):
            check_semantics(obj)

    def test_headless_slots_preserve_unknown_health_units(self):
        legacy = {"index": 0, "present": True, "state": 3, "center_hz": 96100000,
                  "sample_rate": 2400000, "gain_tenth_db": 200,
                  "usb_bytes_received": 5000, "iq_bytes_delivered": 4800,
                  "iq_bytes_consumed": 4000, "drops": 17,
                  "ring_overrun_bytes": 512, "usb_errors": 0}
        state = headless_state(legacy)
        health = headless_health(legacy)
        self.assertEqual("streaming", state["state"])
        self.assertIsNone(state["owner"])
        self.assertEqual("unknown", state["applied"]["gain_mode"])
        self.assertEqual(5000, health["usb_bytes_received"])
        self.assertEqual(4000, health["iq_bytes_consumed"])
        self.assertIsNone(health["dropped_complex_samples"])
        self.assertIsNone(health["effective_sample_rate_sps"])

    def test_headless_absent_and_invalid_slot(self):
        state = headless_state({"index": 0, "present": False, "state": 0})
        self.assertIsNone(state["applied"]["center_frequency_hz"])
        with self.assertRaises(ValueError):
            headless_state({"index": 0, "present": True, "state": 99})

    def test_mobile_snapshot_does_not_invent_identity_or_clock(self):
        state = mobile_state({"streaming": True, "frequencyHz": 101700000, "rateHz": 2048000})
        self.assertEqual(101700000, state["applied"]["center_frequency_hz"])
        self.assertIsNone(state["owner"])
        self.assertIsNone(state["applied"]["gain_tenth_db"])

    def test_preflight_generation_and_attachment_fences(self):
        command = self.example("05-tune-command.json")
        state = self.example("04-receiver-state.json")
        caps = self.example("03-capabilities.json")["payload"]["receiver"]
        ok = preflight_tune(command, state, caps, {"tune"}, principal_id="dashboard-fm")
        self.assertEqual("pending", ok["status"])
        self.assertIsNone(ok["applied"])
        command["payload"]["expected_generation"] = 6
        stale = preflight_tune(command, state, caps, {"tune"}, principal_id="dashboard-fm")
        self.assertEqual("STALE_GENERATION", stale["error"]["code"])
        command["payload"]["expected_generation"] = 7
        command["payload"]["expected_attachment_id"] = "previous-plug"
        changed = preflight_tune(command, state, caps, {"tune"}, principal_id="dashboard-fm")
        self.assertEqual("ATTACHMENT_CHANGED", changed["error"]["code"])

    def test_preflight_denies_missing_tune_scope_and_other_owner(self):
        command = self.example("05-tune-command.json")
        state = self.example("04-receiver-state.json")
        caps = self.example("03-capabilities.json")["payload"]["receiver"]
        denied = preflight_tune(command, state, caps, {"observe"}, principal_id="dashboard-fm")
        self.assertEqual("UNAUTHORIZED", denied["error"]["code"])
        denied = preflight_tune(command, state, caps, {"tune"}, principal_id="other-client")
        self.assertEqual("UNAUTHORIZED", denied["error"]["code"])

    def test_preflight_unsupported_rf_gap_and_sample_rate(self):
        command = self.example("05-tune-command.json")
        state = self.example("04-receiver-state.json")
        caps = self.example("03-capabilities.json")["payload"]["receiver"]
        command["payload"]["requested"] = {"frequency_hz": 7000000}
        bad = preflight_tune(command, state, caps, {"tune"}, principal_id="dashboard-fm")
        self.assertEqual("OUT_OF_RANGE", bad["error"]["code"])
        command["payload"]["requested"] = {"frequency_hz": 101700000, "sample_rate_sps": 777777}
        bad = preflight_tune(command, state, caps, {"tune"}, principal_id="dashboard-fm")
        self.assertEqual("UNSUPPORTED_CAPABILITY", bad["error"]["code"])

    def test_ack_must_be_actual_and_effective(self):
        command = self.example("05-tune-command.json")
        state = self.example("04-receiver-state.json")
        caps = self.example("03-capabilities.json")["payload"]["receiver"]
        pending = preflight_tune(command, state, caps, {"tune"}, principal_id="dashboard-fm")
        fail = complete_tune(command, pending, hardware_ack=False)
        self.assertEqual("HARDWARE_FAILURE", fail["error"]["code"])
        self.assertFalse(fail["backend_ack"])
        with self.assertRaises(ValueError):
            complete_tune(command, pending, hardware_ack=True)
        confirmed = complete_tune(command, pending, hardware_ack=True,
                                  effective=state["payload"]["applied"],
                                  ack_source="verified driver readback")
        self.assertEqual("applied", confirmed["status"])
        self.assertEqual(8, confirmed["result_generation"])
        self.assertTrue(confirmed["backend_ack"])

    def test_unavailable_unsupported_device_no_fake_success(self):
        command = self.example("05-tune-command.json")
        state = self.example("04-receiver-state.json")
        caps = self.example("03-capabilities.json")["payload"]["receiver"]
        caps["frequency_ranges"] = [{"min_hz": 1000000000, "max_hz": 1700000000}]
        result = preflight_tune(command, state, caps, {"tune"}, principal_id="dashboard-fm")
        self.assertEqual("unavailable", result["status"])
        self.assertEqual("OUT_OF_RANGE", result["error"]["code"])

    def test_full_legacy_state_envelope_can_validate(self):
        legacy = headless_state({"index": 0, "present": True, "state": 3,
                                  "center_hz": 96100000, "sample_rate": 2400000})
        exemplar = self.example("04-receiver-state.json")
        exemplar["payload"] = legacy
        self.validator.validate(exemplar)
        check_semantics(exemplar)


if __name__ == "__main__":
    unittest.main()
