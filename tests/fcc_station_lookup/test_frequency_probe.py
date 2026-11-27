import importlib.util
import struct
import tempfile
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location('frequency_probe', Path(__file__).parents[2] / 'tools/fcc_station_lookup/frequency_probe.py')
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


def row(freq, upper=''):
    fields = [''] * 30
    fields[0], fields[10], fields[11] = 'FR', freq, upper
    return ('|'.join(fields) + '\n').encode()


class FrequencyProbeTests(unittest.TestCase):
    def test_groups_exact_frequencies_and_accounts_for_exclusions(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'index'
            report = probe.build([row('100.5'), row('99'), row('100.500000'), row('100','101'), row('bad')], output)
            self.assertEqual((report['rows'], report['indexed'], report['intervals'], report['invalid']), (5, 3, 1, 1))
            self.assertEqual(report['frequencies'], 2)
            data = output.read_bytes()
            self.assertEqual(struct.unpack_from('<8sII', data), (b'FCCEXP01', 2, 3))
            self.assertEqual(struct.unpack_from('<QII', data, 16), (99000000, 0, 1))
            self.assertEqual(struct.unpack_from('<QII', data, 32), (100500000, 1, 2))
            self.assertEqual(struct.unpack_from('<III', data, 48), (2, 1, 3))

    def test_existing_output_is_never_overwritten(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'index'
            output.write_bytes(b'keep')
            with self.assertRaises(FileExistsError): probe.build([row('100')], output)
            self.assertEqual(output.read_bytes(), b'keep')

    def test_bad_schema_fails_without_publishing(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'index'
            with self.assertRaises(ValueError): probe.build([b'FR|bad\n'], output)
            self.assertFalse(output.exists())


if __name__ == '__main__': unittest.main()
