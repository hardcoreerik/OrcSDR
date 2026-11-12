"""Host-only validation of preview semantics; no OrcSDR firmware required."""
import contextlib
import hashlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import import_preview as lib


class PreviewTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)

    def write(self, name, content):
        path = self.root / name
        path.write_text(content, encoding='utf-8')
        return path

    def snapshot(self, profiles, name='existing.json', source=None):
        return self.write(name, json.dumps({'schema': lib.SCHEMA, 'version': 1,
                                           'source': source or {'name': 'user snapshot'},
                                           'profiles': profiles}))

    def preview(self, input_path, existing=None, **kw):
        return lib.build_preview(lib.read_source(input_path), lib.read_source(existing) if existing else None, **kw)

    def test_csv_mhz_defaults_and_provenance(self):
        f = self.write('example.csv', 'Frequency,Mode,Name,CC,Time Slot,Location\n145.5,DMR,Local,3,1,OR\n')
        p = self.preview(f)
        self.assertEqual(p['plan']['create_count'], 1)
        self.assertEqual(p['plan']['creates'][0]['record']['frequency_hz'], 145500000)
        self.assertEqual(p['plan']['creates'][0]['record']['color_code'], 3)
        self.assertEqual(p['plan']['creates'][0]['record']['slot'], 1)
        self.assertEqual(p['source']['sha256'], hashlib.sha256(f.read_bytes()).hexdigest())
        self.assertEqual(p['plan']['creates'][0]['provenance']['claim'], 'imported_profile_not_rf_observation')
        self.assertEqual(p['plan']['creates'][0]['provenance']['source_line'], 2)

    def test_json_schema_and_asserted_source_not_rf(self):
        f = self.snapshot([{'frequency_hz': 446000000, 'protocol': 'DMR'}], 'input.json',
                          {'name': 'manual export', 'url': 'https://example.invalid',
                           'license': 'user-supplied', 'retrieved_at': '2026-10-01'})
        p = self.preview(f)
        self.assertEqual(p['source']['asserted_name'], 'manual export')
        self.assertEqual(p['source']['evidence'], 'directory_candidate')
        self.assertTrue(p['dry_run'])
        self.assertFalse(p['writes_existing_configuration'])

    def test_exact_duplicate_across_files_and_header_order(self):
        e = self.snapshot([{'frequency_hz': 145500000, 'protocol': 'DMR', 'name': 'Beacon'}])
        f = self.write('fresh.csv', 'Name,Mode,Frequency\nBeacon,DMR,145.5\n')
        p = self.preview(f, e)
        self.assertEqual(p['counts']['duplicate'], 1)
        self.assertEqual(p['plan']['create_count'], 0)
        self.assertEqual(p['rows'][0]['matched'], [{'scope': 'existing', 'index': 1}])

    def test_conflict_same_identity_different_annotation_safe_default(self):
        e = self.snapshot([{'frequency_hz': 145500000, 'protocol': 'DMR', 'name': 'User label'}])
        f = self.snapshot([{'frequency_hz': 145500000, 'protocol': 'DMR', 'name': 'Directory name'}], 'incoming.json')
        before = e.read_bytes()
        p = self.preview(f, e)
        self.assertEqual(p['counts']['conflict'], 1)
        self.assertEqual(p['plan']['create_count'], 0)
        self.assertEqual(p['plan']['updates'], [])
        self.assertEqual(e.read_bytes(), before)
        p2 = self.preview(f, e, on_conflict='keep-both', renames=['1=Imported Channel'])
        self.assertEqual(p2['plan']['create_count'], 1)
        self.assertEqual(p2['plan']['creates'][0]['record']['name'], 'Imported Channel')
        self.assertEqual(p2['plan']['updates'], [])
        self.assertEqual(e.read_bytes(), before)

    def test_same_frequency_distinct_sites_preserved(self):
        e = self.snapshot([{'frequency_hz': 451000000, 'protocol': 'DMR', 'role': 'repeater',
                            'site_id': 'A', 'network_id': 'Net'}])
        f = self.snapshot([{'frequency_hz': 451000000, 'protocol': 'DMR', 'role': 'repeater',
                            'site_id': 'B', 'network_id': 'Net'}], 'incoming.json')
        p = self.preview(f, e)
        self.assertEqual(p['counts']['new'], 1)
        self.assertEqual(p['plan']['create_count'], 1)

    def test_same_frequency_missing_identity_requires_review(self):
        e = self.snapshot([{'frequency_hz': 451000000, 'protocol': 'DMR', 'role': 'repeater'}])
        f = self.snapshot([{'frequency_hz': 451000000, 'protocol': 'DMR', 'role': 'repeater',
                            'site_id': 'A'}], 'incoming.json')
        p = self.preview(f, e)
        self.assertEqual(p['counts']['review'], 1)
        self.assertEqual(p['plan']['create_count'], 0)
        self.assertEqual(self.preview(f, e, on_review='keep-both')['plan']['create_count'], 1)

    def test_distinct_protocol_same_frequency(self):
        e = self.snapshot([{'frequency_hz': 145500000, 'protocol': 'DMR'}])
        f = self.snapshot([{'frequency_hz': 145500000, 'protocol': 'P25'}], 'incoming.json')
        self.assertEqual(self.preview(f, e)['counts']['new'], 1)

    def test_within_import_duplicate_and_conflict(self):
        f = self.snapshot([{'frequency_hz': 145500000, 'protocol': 'DMR', 'name': 'A'},
                           {'frequency_hz': 145500000, 'protocol': 'DMR', 'name': 'A'},
                           {'frequency_hz': 145500000, 'protocol': 'DMR', 'name': 'B'}], 'incoming.json')
        p = self.preview(f)
        self.assertEqual([r['status'] for r in p['rows']], ['new', 'duplicate', 'conflict'])
        self.assertEqual(p['plan']['create_count'], 1)

    def test_selection_ranges_and_explicit_override(self):
        f = self.snapshot([{'frequency_hz': i, 'protocol': 'FM'} for i in (50000000, 60000000, 70000000)], 'incoming.json')
        p = self.preview(f, selected='1,3')
        self.assertEqual([x['index'] for x in p['plan']['creates']], [1, 3])
        with self.assertRaises(lib.ImportErrorDetail):
            self.preview(f, selected='1-4')

    def test_mapping_custom_columns(self):
        f = self.write('custom.csv', 'Dial,Digital,Callsign\n433.5,DMR,Repeater\n')
        parsed = lib.read_source(f, lib.overrides(['frequency_mhz=Dial', 'protocol=Digital', 'name=Callsign']))
        p = lib.build_preview(parsed)
        self.assertEqual(p['plan']['creates'][0]['record']['frequency_hz'], 433500000)
        self.assertEqual(p['plan']['creates'][0]['record']['name'], 'Repeater')

    def test_unsupported_csv_fields_reported(self):
        f = self.write('test.csv', 'Frequency,Mode,Vendor Secret\n145.5,DMR,hello\n')
        p = self.preview(f)
        self.assertIn('Vendor Secret', p['warnings'][0])
        self.assertNotIn('Vendor Secret', p['plan']['creates'][0]['record'])

    def test_invalid_rows_no_poison_and_no_create(self):
        f = self.write('test.csv', 'Frequency,Mode,CC,NAC\n145.5,DMR,16,\n,DMR,,\n146.0,DMR,3,293\n433.1,DMR,3,\n')
        p = self.preview(f)
        self.assertEqual(p['counts']['invalid'], 3)
        self.assertEqual(p['counts']['new'], 1)
        self.assertEqual(p['plan']['create_count'], 1)
        self.assertIn('color_code must be', p['rows'][0]['errors'][0])

    def test_receiver_range_warns_not_rejects(self):
        f = self.snapshot([{'frequency_hz': 22000000, 'protocol': 'FM'}], 'incoming.json')
        p = self.preview(f)
        self.assertEqual(p['counts']['new'], 1)
        self.assertIn('outside configured receiver range', p['rows'][0]['warnings'][0])

    def test_json_version_rejected(self):
        f = self.write('input.json', '{"schema":"orcsdr.radio-profiles","version":2,"profiles":[]}')
        with self.assertRaisesRegex(lib.ImportErrorDetail, 'unsupported JSON schema'):
            lib.read_source(f)

    def test_malformed_json_and_bad_existing_fail_closed(self):
        f = self.write('bad.json', '{broken')
        with self.assertRaises(lib.ImportErrorDetail):
            lib.read_source(f)
        good = self.snapshot([{'frequency_hz': 145500000, 'protocol': 'DMR'}], 'incoming.json')
        bad = self.snapshot([{'frequency_hz': 'no', 'protocol': 'DMR'}])
        with self.assertRaisesRegex(lib.ImportErrorDetail, 'existing snapshot row'):
            self.preview(good, bad)

    def test_duplicate_headers_rejected(self):
        f = self.write('bad.csv', 'Frequency,frequency,Mode\n145.5,146.0,DMR\n')
        with self.assertRaises(lib.ImportErrorDetail):
            lib.read_source(f)

    def test_frequency_precision_boolean_and_unknown_protocol(self):
        with self.assertRaises(ValueError):
            lib.frequency('145.5000001', 'frequency_mhz')
        with self.assertRaises(ValueError):
            lib.frequency(True, 'frequency_hz')
        f = self.snapshot([{'frequency_hz': 145500000, 'protocol': 'FUTURE'}], 'incoming.json')
        self.assertEqual(self.preview(f)['counts']['invalid'], 1)

    def test_conflict_fingerprint_stable_independent_provenance(self):
        a = {'frequency_hz': 145500000, 'protocol': 'DMR', 'name': 'same', 'role': 'unknown'}
        b = {'name': 'same', 'role': 'unknown', 'protocol': 'DMR', 'frequency_hz': 145500000}
        self.assertEqual(lib.content_key(a), lib.content_key(b))
        self.assertEqual(lib.identity(a), lib.identity(b))

    def test_read_only_cli_and_exclusive_output(self):
        f = self.snapshot([{'frequency_hz': 145500000, 'protocol': 'DMR'}], 'incoming.json')
        preview = self.root / 'preview.json'
        stdout = io.StringIO()
        with contextlib.redirect_stdout(stdout):
            self.assertEqual(lib.main([str(f), '--format', 'json', '--output', str(preview)]), 0)
        self.assertEqual(stdout.getvalue(), '')
        obj = json.loads(preview.read_text())
        self.assertEqual(obj['plan']['updates'], [])
        original = preview.read_bytes()
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(lib.main([str(f), '--format', 'json', '--output', str(preview)]), 2)
            self.assertEqual(lib.main([str(f), '--output', str(f)]), 2)
        self.assertEqual(preview.read_bytes(), original)

    def test_csv_utf8_bom_and_cli_text(self):
        f = self.write('import.csv', '\ufeffFrequency,Mode\n145.500,FM\n')
        stdout = io.StringIO()
        with contextlib.redirect_stdout(stdout):
            self.assertEqual(lib.main([str(f)]), 0)
        self.assertIn('No radio configurations were written.', stdout.getvalue())

    def test_no_false_merge_of_missing_radio_ids(self):
        e = self.snapshot([{'frequency_hz': 450000000, 'protocol': 'DMR', 'radio_id': 100}])
        f = self.snapshot([{'frequency_hz': 450000000, 'protocol': 'DMR', 'radio_id': 101}], 'incoming.json')
        self.assertEqual(self.preview(f, e)['counts']['new'], 1)

    def test_missing_role_only_review_not_false_new(self):
        e = self.snapshot([{'frequency_hz': 450000000, 'protocol': 'DMR', 'role': 'unknown'}])
        f = self.snapshot([{'frequency_hz': 450000000, 'protocol': 'DMR', 'role': 'repeater'}], 'incoming.json')
        self.assertEqual(self.preview(f, e)['counts']['review'], 1)

    def test_explicit_source_metadata_is_asserted(self):
        f = self.write('test.csv', 'Frequency,Mode\n145.5,FM\n')
        output = self.root / 'preview.json'
        self.assertEqual(lib.main([str(f), '--source-name', 'My directory',
                                   '--source-license', 'check terms',
                                   '--format', 'json', '--output', str(output)]), 0)
        parsed = json.loads(output.read_text())
        self.assertEqual(parsed['source']['asserted_name'], 'My directory')
        self.assertEqual(parsed['source']['asserted_license'], 'check terms')

    def test_mapping_errors_and_malformed_row(self):
        f = self.write('bad.csv', 'Frequency,Mode\n145.5,DMR,EXTRA\n')
        # Extra cell is retained in raw data, but must not be accepted as imported identity.
        p = self.preview(f)
        self.assertEqual(p['counts']['invalid'], 1)
        self.assertIn('more cells', p['rows'][0]['errors'][0])
        with self.assertRaises(lib.ImportErrorDetail):
            lib.read_source(f, lib.overrides(['frequency_mhz=Missing']))
        with self.assertRaises(lib.ImportErrorDetail):
            lib.overrides(['frequency_hz=Frequency', 'frequency_mhz=Frequency'])


if __name__ == '__main__':
    unittest.main(verbosity=2)