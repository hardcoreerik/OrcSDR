import importlib.util
import io
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

TOOL = Path(__file__).resolve().parents[1] / 'tools' / 'test_ft8_dial_device.py'
spec = importlib.util.spec_from_file_location('ft8_dial_device', TOOL)
device = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = device
spec.loader.exec_module(device)


class DeliveryVerdictTests(unittest.TestCase):
    def observation(self, baseline=10, store=64):
        result = device.Observation(baseline)
        result.add('TAB5', f'ORC_FT8_STATUS screen=1 runtime=ready mode=FT8 dial_hz=14074000 clock=1 rx_running=1 tap=1 store={store}')
        result.add('DIAL', 'ORCDIAL_STATUS link=LINKED pairing=0 freq=14074000 dashboard=17 pending=0')
        return result

    def decode(self, result, record=11):
        result.add('TAB5', 'ORC_FT8_RT slot=1793378385 decodes=2 total_ms=2324')
        result.add('DIAL', f'FT8_RX type=17 record={record}')
        result.add('DIAL', f'FT8_RX type=18 record={record}')

    def test_quiet_band_is_inconclusive_not_failure(self):
        self.assertEqual(self.observation().result()[0], 'INCONCLUSIVE')

    def test_repeated_old_card_cannot_pass_live_test(self):
        result = self.observation()
        self.decode(result, record=10)
        self.assertEqual(result.result()[0], 'FAIL')

    def test_new_delivery_passes_even_when_decode_store_is_full(self):
        result = self.observation(store=64)
        self.decode(result)
        self.assertEqual(result.result()[0], 'PASS')

    def test_mismatched_or_out_of_order_text_cannot_pass(self):
        result = self.observation()
        result.add('TAB5', 'ORC_FT8_RT slot=1 decodes=1')
        result.add('DIAL', 'FT8_RX type=18 record=11')
        result.add('DIAL', 'FT8_RX type=17 record=11')
        result.add('DIAL', 'FT8_RX type=18 record=12')
        self.assertEqual(result.result()[0], 'FAIL')
        result.add('DIAL', 'FT8_RX type=18 record=11')
        self.assertEqual(result.result()[0], 'PASS')

    def test_cached_delivery_without_new_rf_is_inconclusive(self):
        result = self.observation()
        result.add('DIAL', 'FT8_RX type=17 record=11')
        result.add('DIAL', 'FT8_RX type=18 record=11')
        self.assertEqual(result.result()[0], 'INCONCLUSIVE')

    def test_link_failure_overrides_success(self):
        result = self.observation()
        self.decode(result)
        result.add('DIAL', 'ORCDIAL_STATUS link=OFFLINE freq=14074000')
        self.assertEqual(result.result()[0], 'FAIL')

    def test_receiver_link_failure_is_recorded(self):
        result = self.observation()
        self.decode(result)
        result.add('TAB5', 'RTL_ORCDIAL_STATUS bridge=1 trust=1 connection=Failed boot=1')
        self.assertEqual(result.result()[0], 'FAIL')

    def test_c6_failure_overrides_successful_decode(self):
        result = self.observation()
        self.decode(result)
        result.add('TAB5', 'RTL_WIFI_C6_STATUS host=3.0.6 transport=0')
        self.assertEqual(result.result()[0], 'FAIL')

    def test_no_clock_is_inconclusive(self):
        result = self.observation()
        self.decode(result)
        result.receiver[-1]['clock'] = '0'
        self.assertEqual(result.result()[0], 'INCONCLUSIVE')

    def test_retune_or_stopped_iq_is_not_a_pass(self):
        for key, value in [('dial_hz', '5357000'), ('rx_running', '0'), ('tap', '0')]:
            with self.subTest(key=key):
                result = self.observation()
                self.decode(result)
                sample = dict(result.receiver[-1])
                sample[key] = value
                result.receiver.append(sample)
                self.assertEqual(result.result()[0], 'FAIL')

    def test_record_wrap_and_duplicate_sequences(self):
        result = self.observation(baseline=0xffffffff)
        self.decode(result, record=1)
        self.assertEqual(result.result()[0], 'PASS')
        self.assertFalse(device.newer(0, 0xffffffff))
        self.assertFalse(device.newer(11, 11))
        self.assertFalse(device.newer(9, 11))


class StatusPollingTests(unittest.TestCase):
    def test_partial_serial_reads_are_not_treated_as_complete_status(self):
        endpoint = type('Endpoint', (), {'s': None})()
        rig = device.Rig(endpoint, endpoint, io.StringIO())
        rig.ingest('DIAL', b'ORCDIAL_STATUS link=LINKED')
        self.assertEqual(rig.rows, [])
        rig.ingest('DIAL', b' freq=14074000 dashboard=17\n')
        self.assertEqual(device.fields(rig.rows[0][1])['freq'], '14074000')

    def test_authentication_material_is_not_written_to_evidence(self):
        endpoint = type('Endpoint', (), {'s': None})()
        log = io.StringIO()
        rig = device.Rig(endpoint, endpoint, log)
        rig.ingest('TAB5', b'PAIR private-key-value\nAUTH_OK private-proof\nRTL_ORCDIAL_STATUS bridge=1\n')
        self.assertNotIn('private-', log.getvalue())
        self.assertEqual(len(rig.rows), 1)

    def rig(self):
        class DelayedRig(device.Rig):
            def __init__(self):
                self.rows, self.sent = [], []
                self.now = 0

            def send(self, who, command):
                self.sent.append(command)

            def pump(self, seconds):
                self.now += seconds
                if len(self.sent) >= 2:
                    self.rows.append(('DIAL', 'ORCDIAL_SECURITY trust=1 connection=Disconnected boot=1'))
        return DelayedRig()

    def test_status_is_polled_until_async_transition_completes(self):
        rig = self.rig()
        with patch.object(device.time, 'monotonic', side_effect=lambda: rig.now):
            line = rig.query('DIAL', 'ORCDIAL_STATUS', 'ORCDIAL_SECURITY trust=1 connection=Disconnected ', timeout=3)
        self.assertIn('Disconnected', line)
        self.assertEqual(rig.sent, ['ORCDIAL_STATUS', 'ORCDIAL_STATUS'])

    def test_mutating_commands_are_not_implicitly_repeated(self):
        rig = self.rig()
        with patch.object(device.time, 'monotonic', side_effect=lambda: rig.now):
            with self.assertRaises(TimeoutError):
                rig.query('DIAL', 'ORCDIAL_ROTATE 1', 'UNUSED', timeout=3)
        self.assertEqual(rig.sent, ['ORCDIAL_ROTATE 1'])


if __name__ == '__main__':
    unittest.main()
