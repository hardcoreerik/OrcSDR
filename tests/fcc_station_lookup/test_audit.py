import importlib.util
import tempfile
import unittest
import zipfile
from pathlib import Path

spec = importlib.util.spec_from_file_location('fcc_audit', Path(__file__).parents[2] / 'tools/fcc_station_lookup/audit.py')
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class ArchiveAuditTests(unittest.TestCase):
    def test_counts_widths_and_invalid_record_types_without_exposing_rows(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'source.zip'
            with zipfile.ZipFile(path, 'w') as archive:
                archive.writestr('FR.dat', 'FR|private|1\nFR|private\nXX|secret|2\n')
                archive.writestr('EM.dat', 'EM|private\n')
            result = audit.audit(path, ('FR', 'EM'))
            self.assertEqual(result['tables']['FR'], {'rows': 3, 'wrong_type': 1, 'widths': {'2': 1, '3': 2}})
            self.assertEqual(result['tables']['EM']['rows'], 1)
            self.assertNotIn('private', str(result))
            self.assertNotIn('secret', str(result))

    def test_missing_or_duplicate_table_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'source.zip'
            with zipfile.ZipFile(path, 'w') as archive:
                archive.writestr('FR.dat', 'FR|1\n')
                archive.writestr('fr.DAT', 'FR|2\n')
            with self.assertRaises(ValueError): audit.audit(path, ('FR',))
            with self.assertRaises(ValueError): audit.audit(path, ('EM',))


if __name__ == '__main__': unittest.main()
