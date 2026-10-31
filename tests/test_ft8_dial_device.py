import importlib.util
import io
import contextlib
import json
import runpy
import tempfile
from types import SimpleNamespace
from pathlib import Path
import sys
import unittest
from unittest.mock import patch, Mock

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
        result.add('TAB5', f'ORC_FT8_DIAL_RECORD record={record} slot=1793378385')
        result.add('DIAL', f'FT8_RX type=17 record={record}')
        result.add('DIAL', f'FT8_RX type=18 record={record}')

    def test_quiet_band_is_inconclusive_not_failure(self):
        self.assertEqual(self.observation().result()[0], 'INCONCLUSIVE')

    def test_late_decode_and_delivery_cannot_change_measured_window(self):
        result = self.observation()
        result.deadline = 10
        with patch.object(device.time, 'monotonic', return_value=11):
            self.decode(result)
        self.assertEqual(result.result()[0], 'INCONCLUSIVE')
        self.assertEqual(result.slots, {})
        self.assertEqual(result.complete, set())

    def test_late_text_cannot_complete_a_record(self):
        result = self.observation()
        result.deadline = 10
        with patch.object(device.time, 'monotonic', return_value=9):
            result.add('TAB5', 'ORC_FT8_RT slot=1 decodes=1')
            result.add('TAB5', 'ORC_FT8_DIAL_RECORD record=11 slot=1')
            result.add('DIAL', 'FT8_RX type=17 record=11')
        with patch.object(device.time, 'monotonic', return_value=10):
            result.add('DIAL', 'FT8_RX type=18 record=11')
        self.assertEqual(result.result()[0], 'FAIL')

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
        result.add('TAB5', 'ORC_FT8_DIAL_RECORD record=11 slot=1')
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

    def test_delayed_record_from_another_slot_cannot_pass(self):
        result = self.observation()
        self.decode(result)
        result.record_slots[11] = '1793378370'
        self.assertEqual(result.result()[0], 'FAIL')

    def test_old_firmware_without_record_slot_evidence_is_inconclusive(self):
        result = self.observation()
        self.decode(result)
        result.record_slots.clear()
        self.assertEqual(result.result()[0], 'INCONCLUSIVE')

    def test_one_missing_mapping_does_not_claim_transport_failure(self):
        result = self.observation()
        self.decode(result)
        result.record_slots[11] = '1793378370'
        result.add('DIAL', 'FT8_RX type=17 record=12')
        result.add('DIAL', 'FT8_RX type=18 record=12')
        self.assertEqual(result.result()[0], 'INCONCLUSIVE')


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


class FailureReportingTests(unittest.TestCase):
    def argv(self, output):
        return ['runner', '--tab5-port', 'COM17', '--dial-port', 'COM14',
                '--antenna', 'test fixture', '--output', str(output)]

    def test_missing_git_still_writes_failure_evidence(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / 'run'
            with patch.object(sys, 'argv', self.argv(output)), \
                    patch.object(device.subprocess, 'check_output', side_effect=FileNotFoundError('git missing')), \
                    contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(device.main(), 1)
            self.assertEqual(json.loads((output / 'report.json').read_text())['overall'], 'FAIL')
            self.assertTrue((output / 'serial.log').exists())

    def test_serial_close_failure_does_not_hide_original_failure_or_report(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / 'run'
            ports = [SimpleNamespace(s=Mock(), authenticate=Mock(side_effect=RuntimeError('auth failed'))),
                     SimpleNamespace(s=Mock())]
            ports[0].s.close.side_effect=OSError('close failed')
            fake=SimpleNamespace(Tab5=Mock(side_effect=ports),find_key=lambda path:'ab'*32)
            with patch.object(sys,'argv',self.argv(output)), \
                    patch.object(device.subprocess,'check_output',return_value='test-head'), \
                    patch.dict(sys.modules,{'tab5_ft8':fake,'tab5_ft8_shot':SimpleNamespace(snap_png=Mock())}), \
                    contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(device.main(),1)
            report=json.loads((output/'report.json').read_text())
            self.assertEqual(report['error'],'auth failed')
            self.assertIn('close failed',report['cleanup_errors'][0])
            ports[1].s.close.assert_called_once()

    def test_authentication_echoes_never_reach_console_or_report(self):
        auth_spec = importlib.util.spec_from_file_location('auth_control', TOOL.with_name('tab5_ft8.py'))
        auth = importlib.util.module_from_spec(auth_spec)
        with patch.dict(sys.modules, {'serial': Mock()}):
            auth_spec.loader.exec_module(auth)
        for replies in [([['PAIR ' + 'ab' * 32]]),
                        ([['PAIR_OK'], ['AUTH private-nonce private-proof']])]:
            with self.subTest(replies=replies), tempfile.TemporaryDirectory() as temp:
                output = Path(temp) / 'run'
                endpoints = []
                def endpoint(port):
                    item = SimpleNamespace(s=Mock(), send=Mock(), read_until=Mock(side_effect=replies))
                    item.authenticate = lambda key: auth.Tab5.authenticate(item, key)
                    endpoints.append(item)
                    return item
                fake = SimpleNamespace(Tab5=endpoint, find_key=lambda path: 'ab' * 32)
                console = io.StringIO()
                with patch.object(sys, 'argv', self.argv(output)), \
                        patch.object(device.subprocess, 'check_output', return_value='test-head'), \
                        patch.dict(sys.modules, {'tab5_ft8': fake, 'tab5_ft8_shot': SimpleNamespace(snap_png=Mock())}), \
                        contextlib.redirect_stdout(console):
                    self.assertEqual(device.main(), 1)
                evidence = console.getvalue() + (output / 'report.json').read_text() + (output / 'serial.log').read_text()
                self.assertNotIn('ab' * 32, evidence)
                self.assertNotIn('private-', evidence)
                for item in endpoints:
                    item.s.close.assert_called_once()

    def test_sstv_second_port_failure_closes_first_and_reports(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / 'run'
            key = Path(temp) / 'key'; key.write_text('ab' * 32)
            first = SimpleNamespace(close=Mock())
            argv = ['sstv', '--tab5-port', 'COM17', '--dial-port', 'COM14', '--key', str(key),
                    '--frequency', '14230000', '--out', str(output)]
            with patch.object(sys, 'argv', argv), \
                    patch.dict(sys.modules, {'help_media': SimpleNamespace(Tab5=Mock(return_value=first)),
                                            'tab5_ft8': SimpleNamespace(Tab5=Mock(side_effect=OSError('port busy')))}), \
                    contextlib.redirect_stdout(io.StringIO()), self.assertRaises(SystemExit) as stopped:
                runpy.run_path(str(TOOL.with_name('test_sstv_dial_device.py')), run_name='__main__')
            self.assertEqual(stopped.exception.code, 1)
            first.close.assert_called_once()
            self.assertEqual(json.loads((output / 'device-result.json').read_text())['overall'], 'FAIL')

    def test_sstv_auth_helper_omits_rejected_proof(self):
        helper_spec = importlib.util.spec_from_file_location('media_auth', TOOL.with_name('help_media.py'))
        helper = importlib.util.module_from_spec(helper_spec)
        helper_spec.loader.exec_module(helper)
        with tempfile.TemporaryDirectory() as temp:
            key_path = Path(temp) / 'test.key'
            key_path.write_text('ab' * 32)
            endpoint = SimpleNamespace(key_path=key_path, send=Mock(),
                                       wait=Mock(side_effect=['PAIR_OK', 'AUTH_OK private-proof']))
            with self.assertRaises(RuntimeError) as failure:
                helper.Tab5.authenticate(endpoint)
            self.assertNotIn('private-', str(failure.exception))
            self.assertNotIn('ab' * 32, str(failure.exception))

    def test_existing_key_mode_does_not_create_key_or_send_pair(self):
        helper_spec = importlib.util.spec_from_file_location('existing_key_auth', TOOL.with_name('help_media.py'))
        helper = importlib.util.module_from_spec(helper_spec)
        helper_spec.loader.exec_module(helper)
        with tempfile.TemporaryDirectory() as temp:
            key_path=Path(temp)/'missing.key'
            endpoint=SimpleNamespace(key_path=key_path,send=Mock())
            with self.assertRaises(FileNotFoundError):
                helper.Tab5.authenticate(endpoint,create_key=False)
            self.assertFalse(key_path.exists());endpoint.send.assert_not_called()



if __name__ == '__main__':
    unittest.main()
