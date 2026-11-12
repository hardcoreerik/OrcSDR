"""Deterministic host-only P25 v2 migration preview tests (no firmware/import)."""
import hashlib
import json
import pathlib
import shutil
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import migration as m  # noqa: E402

SAMPLE = ROOT / 'fixtures' / 'sample_sd'


def cfg(name='Sample', freq=851012500, extra=''):
    return f'version=2\nsystem_name={name}\ncontrol_channel_hz={freq}\n{extra}'


class MigrationPreviewTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name) / 'sd'
        (self.root / 'orcsdr' / 'p25').mkdir(parents=True)

    def add(self, pid, text):
        p = self.root / 'orcsdr' / 'p25' / pid / 'profile.cfg'
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text, encoding='utf-8')
        return p

    def generate(self, cap=16, occupied=0, baseline=None):
        return m.preview(self.root, cap, occupied, baseline)

    def test_sample_has_three_creates_two_duplicate_and_active(self):
        p = m.preview(SAMPLE, 8, 1)
        self.assertEqual(p['summary']['status_counts'], {'duplicate': 2, 'new': 3})
        self.assertEqual(len(p['plan']['creates']), 3)
        self.assertEqual([x['id'] for x in p['profiles']], ['alpha', 'alpha-clone', 'bravo'])
        self.assertTrue(p['profiles'][0]['active'])
        self.assertFalse(p['profiles'][1]['active'])
        self.assertEqual(p['profiles'][0]['sidecar']['talkgroups'],
                         [{'id': 123, 'alias': 'Dispatch'}, {'id': 456, 'alias': 'Operations'}])
        self.assertEqual(p['profiles'][0]['sidecar']['hold_talkgroup'], 123)
        self.assertEqual(p['profiles'][0]['sidecar']['last_control_channel_hz'], 852012500)
        self.assertFalse(p['plan']['can_apply'])
        self.assertEqual(p['plan']['updates'], [])
        self.assertEqual(p['plan']['deletes'], [])

    def test_stable_id_and_order_independent_of_root_path(self):
        other = pathlib.Path(self.temp.name) / 'different_root'
        shutil.copytree(SAMPLE, other)
        self.assertEqual(m.preview(SAMPLE, 8, 1), m.preview(other, 8, 1))
        x = m.preview(SAMPLE, 8, 1)
        key = x.pop('plan_sha256')
        self.assertEqual(m.digest_json(x), key)

    def test_source_byte_hash_and_relative_path(self):
        p = m.preview(SAMPLE, 8, 1)
        expected = hashlib.sha256((SAMPLE / 'orcsdr/p25/alpha/profile.cfg').read_bytes()).hexdigest()
        self.assertEqual(p['profiles'][0]['source_sha256'], expected)
        self.assertEqual(p['profiles'][0]['source_path'], 'orcsdr/p25/alpha/profile.cfg')
        self.assertEqual(p['profiles'][0]['records'][0]['model']['source']['sha256'], expected)
        self.assertTrue(p['profiles'][0]['records'][0]['model']['source']['sha_known'])
        self.assertNotIn(str(SAMPLE), json.dumps(p))

    def test_discovery_contract_identity_and_candidate_not_rf(self):
        p = m.preview(SAMPLE, 8, 1)
        model = p['profiles'][0]['records'][0]['model']
        i = model['candidate']['identity']
        self.assertEqual(i, {'known_fields': 13,
                             'access_code': 356, 'system_id': (781824 << 12) | 101,
                             'site_id': 0x0102})
        self.assertEqual(model['candidate']['origin'], 'directory_import')
        self.assertEqual(model['candidate']['proof'], 'candidate')
        self.assertTrue(p['destination_contract']['not_rf_observation'])

    def test_explicit_zero_is_known_but_implicit_zero_is_not(self):
        a = m.parse_profile(cfg(extra='nac=0\nwacn=0\nsystem_id=0\nrfss=0\nsite=0\n').encode())
        b = m.parse_profile(cfg().encode())
        self.assertEqual(m.identity(a['config'], a['explicit_fields']),
                         {'known_fields': 13,
                          'access_code': 0, 'system_id': 0, 'site_id': 0})
        self.assertEqual(m.identity(b['config'], b['explicit_fields']), {'known_fields': 0})

    def test_ambiguous_partial_identity_flagged_without_inventing_known_field(self):
        self.add('a', cfg(extra='wacn=123\nrfss=2\n'))
        p = self.generate()
        i = p['profiles'][0]['records'][0]['model']['candidate']['identity']
        self.assertEqual(i, {'known_fields': 0})
        self.assertEqual(len(p['profiles'][0]['partial_identity']), 3)
        self.assertEqual(p['profiles'][0]['sidecar']['raw_p25_identity']['wacn'], 123)

    def test_p25_v1_is_rejected_no_version_inference(self):
        self.add('a', 'version=1\nsystem_name=Old\ncontrol_channel_hz=851012500\n')
        p = self.generate()
        self.assertEqual(p['profiles'][0]['records'][0]['status'], 'blocked_source')
        self.assertEqual(p['plan']['creates'], [])
        self.assertTrue(p['summary']['source_errors'])

    def test_missing_version_is_rejected(self):
        self.add('a', 'system_name=NoVersion\ncontrol_channel_hz=851012500\n')
        self.assertIn('missing_version', [x['code'] for x in self.generate()['profiles'][0]['errors']])

    def test_repeated_scalar_is_not_silently_last_wins(self):
        self.add('a', cfg(extra='system_name=HiddenOverride\n'))
        p = self.generate()['profiles'][0]
        self.assertIn('duplicate_key', [x['code'] for x in p['errors']])
        self.assertEqual(p['sidecar']['system_name'], 'Sample')
        self.assertEqual(len([e for e in p['raw_entries'] if e['key'] == 'system_name']), 2)

    def test_repeated_control_channel_is_invalid(self):
        self.add('a', cfg(extra='control_channel_hz=851012500\n'))
        p = self.generate()['profiles'][0]
        self.assertEqual(p['records'][0]['status'], 'blocked_source')
        self.assertIn('duplicate', json.dumps(p['errors']))

    def test_repeated_talkgroup_id_is_invalid(self):
        self.add('a', cfg(extra='talkgroup=123,A\ntalkgroup=123,B\n'))
        self.assertEqual(self.generate()['profiles'][0]['records'][0]['status'], 'blocked_source')

    def test_bad_range_and_unrecognized_bool_modulation(self):
        for line in ('nac=4096', 'wacn=1048576', 'control_channel_hz=12',
                     'auto_follow=yes', 'modulation=p25p2', 'cqpsk_timing_gain=nan',
                     'cqpsk_carrier_gain=0.11', 'hold_talkgroup=-1'):
            with self.subTest(line=line):
                parsed = m.parse_profile((cfg(extra=line+'\n')).encode())
                self.assertTrue(parsed['errors'])

    def test_orphan_last_channel_is_invalid(self):
        self.add('a', cfg(extra='last_control_channel_hz=851025000\n'))
        self.assertIn('orphan_last_channel', [e['code'] for e in self.generate()['profiles'][0]['errors']])

    def test_overlong_lines_and_invalid_utf8(self):
        self.assertIn('line_too_long', [e['code'] for e in
                     m.parse_profile((cfg() + 'a=' + 'x'*150 + '\n').encode())['errors']])
        self.assertIn('invalid_utf8', [e['code'] for e in m.parse_profile(b'\xff\xfe')['errors']])
        self.assertIn('oversized_file', [e['code'] for e in m.parse_profile(b'x'*65537)['errors']])

    def test_unknown_keys_explicitly_preserved_and_blocked(self):
        self.add('a', cfg(extra='vendor_encryption=magic\n'))
        x = self.generate()['profiles'][0]
        self.assertEqual(x['records'][0]['status'], 'blocked_source')
        self.assertEqual(x['unsupported_fields'], [{'key': 'vendor_encryption', 'value': 'magic', 'line': 4}])
        self.assertEqual(x['sidecar']['unsupported_fields'], x['unsupported_fields'])

    def test_five_or_more_talkgroups_not_truncated(self):
        extras = ''.join(f'talkgroup={i},Unit {i}\n' for i in range(1, 11))
        self.add('a', cfg(extra=extras))
        x = self.generate()['profiles'][0]
        self.assertEqual(len(x['sidecar']['talkgroups']), 10)
        self.assertIn('source_talkgroup_capacity', [e['code'] for e in x['errors']])
        self.assertEqual(x['records'][0]['status'], 'blocked_source')

    def test_more_than_eight_control_channels_not_truncated(self):
        extras = ''.join(f'control_channel_hz={850000000+i*25000}\n' for i in range(1, 10))
        self.add('a', cfg(extra=extras))
        x = self.generate()['profiles'][0]
        self.assertEqual(len(x['records']), 10)
        self.assertEqual(len(x['sidecar']['control_channels_hz']), 10)
        self.assertIn('source_channel_capacity', [e['code'] for e in x['errors']])
        self.assertFalse(self.generate()['plan']['creates'])

    def test_equivalent_duplicate_ignores_file_name(self):
        self.add('a', cfg())
        self.add('b', cfg())
        p = self.generate()
        self.assertEqual([x['records'][0]['status'] for x in p['profiles']], ['new', 'duplicate'])
        self.assertEqual(len(p['plan']['creates']), 1)

    def test_conflicting_same_known_identity_blocks_both(self):
        self.add('a', cfg(name='Name A', extra='nac=1\nwacn=50\nsystem_id=10\nrfss=1\nsite=2\n'))
        self.add('b', cfg(name='Name B', extra='nac=1\nwacn=50\nsystem_id=10\nrfss=1\nsite=2\n'))
        p = self.generate()
        self.assertEqual([x['records'][0]['status'] for x in p['profiles']], ['conflict', 'conflict'])
        self.assertEqual(p['plan']['creates'], [])

    def test_incomplete_identity_overlap_blocks_both(self):
        self.add('a', cfg(extra='nac=5\nwacn=5\nsystem_id=4\n'))
        self.add('b', cfg(name='Other', extra='nac=5\n'))
        p = self.generate()
        self.assertEqual([x['records'][0]['status'] for x in p['profiles']], ['ambiguous', 'ambiguous'])
        self.assertFalse(p['plan']['creates'])

    def test_distinct_known_identity_same_frequency_can_coexist(self):
        self.add('a', cfg(extra='nac=1\n'))
        self.add('b', cfg(name='Other', extra='nac=2\n'))
        self.assertEqual(self.generate()['summary']['proposed_directory_creates'], 2)

    def test_capacity_exact_and_overflow_are_reported_not_truncated(self):
        self.add('a', cfg(freq=851012500))
        self.add('b', cfg(freq=852012500))
        p = self.generate(cap=2, occupied=1)
        self.assertEqual(p['summary']['status_counts'], {'blocked_capacity': 1, 'new': 1})
        self.assertEqual(p['plan']['creates'][0]['profile_id'], 'a')
        self.assertEqual(len(p['profiles']), 2)
        self.assertEqual(self.generate(cap=2, occupied=2)['plan']['creates'], [])

    def test_missing_capacity_never_suggests_create(self):
        self.add('a', cfg())
        p = m.preview(self.root)
        self.assertEqual(p['profiles'][0]['records'][0]['status'], 'capacity_unverified')
        self.assertEqual(p['plan']['creates'], [])
        with self.assertRaises(m.MigrationError):
            m.preview(self.root, directory_capacity=16)
        with self.assertRaises(m.MigrationError):
            m.preview(self.root, directory_capacity=1, existing_count=2)

    def test_source_profile_limit_reports_all_entries(self):
        for i in range(17):
            self.add(f'prof{i:02d}', cfg(name=f'Site {i}', freq=851000000 + i*25000))
        p = self.generate(cap=32)
        self.assertEqual(len(p['profiles']), 17)
        self.assertEqual(len(p['plan']['creates']), 16)
        self.assertIn('source_profile_capacity', [e['code'] for e in p['discovery_issues']])
        self.assertEqual(p['profiles'][-1]['records'][0]['status'], 'blocked_source')

    def test_active_file_valid_and_dangling(self):
        self.add('a', cfg())
        a = self.root/'orcsdr/p25/active.txt'
        a.write_text('a\n')
        self.assertTrue(self.generate()['profiles'][0]['active'])
        a.write_text('missing\n')
        p = self.generate()
        self.assertFalse(p['profiles'][0]['active'])
        self.assertIn('dangling_active', [x['code'] for x in p['discovery_issues']])

    def test_legacy_profile_detected_with_coexistence_warning(self):
        self.add('a', cfg())
        (self.root/'orcsdr/P25.cfg').write_text(cfg(name='Legacy',freq=156000000))
        p = self.generate()
        self.assertEqual(len(p['profiles']), 2)
        self.assertEqual(p['profiles'][1]['source_path'], 'orcsdr/P25.cfg')
        self.assertIn('legacy_coexists', [e['code'] for e in p['discovery_issues']])

    def test_missing_primary_with_backup_is_flagged(self):
        d = self.root/'orcsdr/p25/backedup'
        d.mkdir()
        (d/'profile.cfg.bak').write_text(cfg())
        p = self.generate()
        self.assertIn('backup_only', [e['code'] for e in p['discovery_issues']])
        self.assertEqual(p['summary']['source_profiles'], 0)

    def test_symlink_refused_and_not_followed(self):
        outside = pathlib.Path(self.temp.name)/'out.cfg'
        outside.write_text(cfg())
        d = self.root/'orcsdr/p25/symlink'
        d.mkdir()
        try:
            (d/'profile.cfg').symlink_to(outside)
        except (OSError, NotImplementedError):
            self.skipTest('symlinks are not permitted on this host')
        p = self.generate()
        self.assertEqual(p['profiles'], [])
        self.assertIn('symlink_blocked', [e['code'] for e in p['discovery_issues']])

    def test_existing_candidate_duplicate_conflict_ambiguity(self):
        self.add('a', cfg(extra='nac=6\n'))
        source_hash = hashlib.sha256((self.root/'orcsdr/p25/a/profile.cfg').read_bytes()).hexdigest()
        ex = pathlib.Path(self.temp.name)/'old.json'
        def baseline(identity, label='Sample', digest=source_hash):
            ex.write_text(json.dumps({'schema':'orcsdr.radio-discovery-candidates','version':1,
                                      'records':[{'protocol':'p25_phase1', 'frequency_hz':851012500,
                                                  'identity':identity,'label':label,'source_sha256':digest}]}))
            return self.generate(cap=2,occupied=1,baseline=ex)
        p = baseline({'known_fields':1, 'access_code':6})
        self.assertEqual(p['profiles'][0]['records'][0]['status'], 'duplicate')
        p = baseline({'known_fields':1, 'access_code':6}, label='Different')
        self.assertEqual(p['profiles'][0]['records'][0]['status'], 'conflict')
        p = baseline({'known_fields':0}, label='Other')
        self.assertEqual(p['profiles'][0]['records'][0]['status'], 'ambiguous')

    def test_invalid_existing_snapshot_refused(self):
        self.add('a', cfg())
        ex = pathlib.Path(self.temp.name)/'old.json'
        ex.write_text('{"schema":"other","version":1,"records":[]}')
        with self.assertRaises(m.MigrationError):
            self.generate(baseline=ex)
        ex.write_text('{"schema":"orcsdr.radio-discovery-candidates","version":1,"records":[] ,"extra":1}')
        with self.assertRaises(m.MigrationError):
            self.generate(baseline=ex)

    def test_write_new_plan_only_without_overwriting_any_input(self):
        src = self.add('a', cfg())
        original = src.read_bytes()
        output = pathlib.Path(self.temp.name)/'outside.json'
        p = self.generate()
        m.export_json(p, output, self.root, None)
        self.assertEqual(json.loads(output.read_text())['plan_sha256'], p['plan_sha256'])
        with self.assertRaises(m.MigrationError):
            m.export_json(p, output, self.root, None)
        with self.assertRaises(m.MigrationError):
            m.export_json(p, self.root/'orcsdr/p25/unsafe.json', self.root, None)
        with self.assertRaises(m.MigrationError):
            m.export_json(p, src, self.root, None)
        self.assertEqual(src.read_bytes(), original)

    def test_single_file_mode(self):
        source = self.add('a', cfg(extra='nac=9\n'))
        p = m.preview(source, 2, 0)
        self.assertEqual(p['input']['mode'], 'single')
        self.assertEqual(p['profiles'][0]['source_path'], 'profile.cfg')
        self.assertEqual(p['summary']['proposed_directory_creates'], 1)


if __name__ == '__main__':
    unittest.main()

# Additional regression classes intentionally kept in the same host test module.
class InputSafetyTests(unittest.TestCase):
    setUp = MigrationPreviewTests.setUp
    add = MigrationPreviewTests.add
    generate = MigrationPreviewTests.generate
    def test_case_collisions_block_both_files(self):
        self.add('Alpha', cfg())
        self.add('alpha', cfg(name='Other',freq=852012500))
        p = self.generate()
        self.assertEqual(len(p['profiles']), 2)
        self.assertTrue(all(x['records'][0]['status'] == 'blocked_source' for x in p['profiles']))
        self.assertEqual(p['plan']['creates'], [])
        self.assertIn('profile_id_case_collision', [x['code'] for x in p['discovery_issues']])

    def test_duplicate_existing_json_key_is_refused(self):
        self.add('a', cfg())
        ex = pathlib.Path(self.temp.name)/'existing.json'
        ex.write_text('{"schema":"orcsdr.radio-discovery-candidates", "version":1, "version":1, "records":[]}')
        with self.assertRaises(m.MigrationError):
            self.generate(baseline=ex)

    def test_bounded_regular_file_reader(self):
        src = self.add('a', cfg())
        self.assertEqual(m.read_bounded(src), src.read_bytes())
        src.write_bytes(b'1' * (m.MAX_FILE + 1))
        self.assertIsNone(m.read_bounded(src))

    def test_export_follows_no_symlink_parent_into_source(self):
        self.add('a', cfg())
        link = pathlib.Path(self.temp.name)/'redirect'
        try:
            link.symlink_to(self.root, target_is_directory=True)
        except (OSError, NotImplementedError):
            self.skipTest('symlinks unsupported')
        with self.assertRaises(m.MigrationError):
            m.export_json(self.generate(), link/'report.json', self.root, None)

    def test_legacy_and_directory_profile_same_id_are_blocked(self):
        self.add('legacy-P25', cfg())
        (self.root/'orcsdr/P25.cfg').write_text(cfg(name='Old', freq=155000000))
        p = self.generate()
        self.assertEqual(len(p['profiles']), 2)
        self.assertEqual(p['plan']['creates'], [])
        self.assertTrue(all(x['records'][0]['status'] == 'blocked_source' for x in p['profiles']))

    def test_legacy_only_sd_root(self):
        shutil.rmtree(self.root/'orcsdr/p25')
        (self.root/'orcsdr/P25.cfg').write_text(cfg())
        p = self.generate()
        self.assertEqual(p['summary']['source_profiles'], 1)
        self.assertEqual(p['profiles'][0]['source_path'], 'orcsdr/P25.cfg')
        self.assertEqual(p['profiles'][0]['records'][0]['status'], 'new')
