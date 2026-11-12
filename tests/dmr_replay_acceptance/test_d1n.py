#!/usr/bin/env python3
"""D1-N hermetic host tests; fake bridge is a harness stub, NOT RF evidence."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import negative_control_repro as neg
import d1n_matrix as matrix


class FakeGate:
    """Only used in fake-API harness unit tests; never called in D1-N CLI."""
    def read_manifest(self, path):
        doc = json.loads(path.read_text())
        return doc, {r['id']: r for r in doc['files']}

    def verify_local(self, root, row):
        target = root / row['path']
        base = {'id': row['id'], 'expected_bytes': row['bytes']}
        if not target.is_file():
            return {**base, 'status': 'missing'}
        raw = target.read_bytes()
        observed = neg.fingerprints(raw)
        if observed['bytes'] != row['bytes'] or observed['git_blob_sha1'] != row['git_blob_sha1'] or (
                row.get('sha256') and observed['sha256'] != row['sha256']):
            return {**base, 'status': 'mismatch'}
        return {**base, 'status': 'verified', 'samples': len(raw)//4,
                'observed_sha256': observed['sha256']}

    def bridge_identity(self, binary):
        if binary is None or not binary.is_file():
            raise ValueError('missing binary')
        return {'name': binary.name, 'sha256': hashlib.sha256(binary.read_bytes()).hexdigest()}

    def metrics_comparable(self, raw):
        if not raw.get('event_trace_sha256'):
            raise ValueError('missing event fingerprint')
        return raw

    def assess_fixture(self, fid, metrics, upstream):
        return {'status': 'PASS', 'issues': [], 'upstream_disagreements': []}


class FakeAPI:
    """Returns canned *synthetic* summaries, never represented as native IQ."""
    def __init__(self, tweak=None):
        self.calls = []
        self.tweak = tweak

    def run_bridge(self, root, row, command, chunk_samples=4096, timeout_s=240):
        fid = row['id']
        self.calls.append((fid, command[0], chunk_samples))
        data = (root / row['path']).read_bytes()
        negative = fid in matrix.IDS[2:]
        samples = len(data) // 4
        metrics = {
            'event_trace_sha256': hashlib.sha256((fid + ' synthetic mock trace').encode()).hexdigest(),
            'bridge_samples_consumed': samples,
            'sync_total': 0 if negative else 11,
            'sync': {'direct_ts1_data': 0 if negative else 8,
                     'direct_ts1_voice': 0 if negative else 3},
            'bursts': 0 if negative else 11,
            'fec': {'pass': 0 if negative else 9, 'fail': 0, 'unknown': 0},
            'check': {'pass': 0 if negative else 9, 'fail': 0, 'unknown': 0},
            'crc': {'pass': 0, 'fail': 0, 'unknown': 0},
            'rs': {'pass': 0 if negative else 9, 'fail': 0, 'unknown': 0},
            'embedded_lc': {'pass': 0, 'fail': 0, 'unknown': 0},
            'check_and_fec_valid': 0 if negative else 9,
            'slots_observed': [] if negative else [1],
            'slots_check_fec_bursts': [] if negative else [1],
            'color_codes': [] if negative else [1],
            'source_ids': [] if negative else [3024109],
            'talkgroups': [] if negative else [99],
            'false_identity_detections': 0,
            'unverified_identity_claims': 0,
            'emitted_rejected_bursts': 0,
        }
        result = {'iq_samples_streamed': samples,
                  'sha256_streamed': hashlib.sha256(data).hexdigest(),
                  'metrics': metrics}
        if self.tweak:
            self.tweak(result, fid, chunk_samples)
        return result


class D1NNegativeReproductionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.dir = Path(self.temp.name)
        self.manifest = neg.MANIFEST

    def test_noise_exact_sha1_sha256_samples(self):
        data = neg.generate_noise()
        fp = neg.fingerprints(data)
        self.assertEqual(fp['bytes'], 384000)
        self.assertEqual(fp['samples'], 96000)
        self.assertEqual(fp['git_blob_sha1'], '3d872e2afb5aa8759520657c2c1c25fbd03acab7')
        self.assertEqual(fp['sha256'], 'ae4f5f6d0ab0f3b8aec9a94cfed8dbadbbc13f94db8758fc2c5bd03b1bdcef71')

    def test_am_exact_sha1_sha256_samples(self):
        fp = neg.fingerprints(neg.generate_am())
        self.assertEqual(fp['bytes'], 384000)
        self.assertEqual(fp['samples'], 96000)
        self.assertEqual(fp['git_blob_sha1'], 'f8873579fe47229866fb2b1576726a22bfc4aa5d')
        self.assertEqual(fp['sha256'], '32d7ea0c83f7ca4168337c90c06e491f1a259dce8241fd8d4b1c5839986aa87e')

    def test_pinned_manifest_matches_recipe(self):
        output = neg.reproduce(self.manifest)
        self.assertEqual(output['status'], 'PASS')
        self.assertEqual([i['regenerated_hashes_match'] for i in output['fixtures']], [True, True])
        self.assertTrue(all(i['external_source_comparison'] == 'not_requested' for i in output['fixtures']))

    def test_same_length_mutated_pin_rejected(self):
        doc = json.loads(self.manifest.read_text())
        next(r for r in doc['files'] if r['id'] == neg.IDS[0])['sha256'] = 'f'*64
        path = self.dir / 'bad.json'
        path.write_text(json.dumps(doc))
        output = neg.reproduce(path)
        self.assertEqual(output['status'], 'FAIL')
        self.assertFalse(output['fixtures'][0]['regenerated_hashes_match'])

    def test_changed_expected_size_rejected(self):
        doc = json.loads(self.manifest.read_text())
        next(r for r in doc['files'] if r['id'] == neg.IDS[0])['bytes'] = 384004
        path = self.dir / 'bad.json'
        path.write_text(json.dumps(doc))
        with self.assertRaises(neg.ReproductionError):
            neg.reproduce(path)

    def test_regenerate_compare_exact_external(self):
        root = self.dir / 'private'
        root.mkdir()
        for name, data in [('auto_noise_seed13579bdf_48k_cs16.cs16', neg.generate_noise()),
                           ('auto_am_envelope_200hz_48k_cs16.cs16', neg.generate_am())]:
            p = root / 'AUTO' / name
            p.parent.mkdir(exist_ok=True)
            p.write_bytes(data)
        report = neg.reproduce(self.manifest, root)
        self.assertEqual(report['status'], 'PASS')
        self.assertEqual([r['external_source_comparison'] for r in report['fixtures']], ['match', 'match'])

    def test_external_tamper_even_same_byte_length(self):
        root = self.dir / 'private'
        root.mkdir()
        for fid, data in zip(neg.IDS, (neg.generate_noise(), neg.generate_am())):
            name = neg._rows(self.manifest)[fid]['path']
            p = root / name
            p.parent.mkdir(exist_ok=True)
            p.write_bytes(data)
        p = root / neg._rows(self.manifest)[neg.IDS[0]]['path']
        modified = bytearray(p.read_bytes())
        modified[2] ^= 0x01
        p.write_bytes(modified)
        self.assertEqual(neg.reproduce(self.manifest, root)['status'], 'FAIL')

    def test_write_is_external_only_exclusive(self):
        root = self.dir / 'generated'
        self.assertEqual(neg.reproduce(self.manifest, root, write=True)['status'], 'PASS')
        with self.assertRaises(FileExistsError):
            neg.reproduce(self.manifest, root, write=True)

    def test_write_inside_repo_rejected(self):
        with self.assertRaises(neg.ReproductionError):
            neg.reproduce(self.manifest, neg.HERE / 'forbidden', write=True)
        self.assertFalse((neg.HERE / 'forbidden').exists())

    def test_missing_external_controls_are_blocked_not_zero(self):
        output = neg.reproduce(self.manifest, self.dir)
        self.assertEqual(output['status'], 'BLOCKED')
        self.assertEqual([r['external_source_comparison'] for r in output['fixtures']], ['missing', 'missing'])


class D1NMatrixTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.dir = Path(self.temp.name)
        self.root = self.dir / 'private'
        self.root.mkdir()
        self.manifest = self.dir / 'manifest.json'
        doc = json.loads(neg.MANIFEST.read_text())
        doc['files'] = [next(r for r in doc['files'] if r['id'] == fid) for fid in matrix.IDS]
        for fid, data in zip(matrix.IDS[:2], [struct.pack('<hh', 1, 0) * 12, struct.pack('<hh', 2, 1) * 13]):
            row = next(r for r in doc['files'] if r['id'] == fid)
            row['bytes'] = len(data)
            row['git_blob_sha1'] = neg.fingerprints(data)['git_blob_sha1']
            self.write(row['path'], data)
        self.write(doc['files'][2]['path'], neg.generate_noise())
        self.write(doc['files'][3]['path'], neg.generate_am())
        self.manifest.write_text(json.dumps(doc))
        self.release = self.dir / 'native-release'
        self.sanitize = self.dir / 'native-asan'
        self.release.write_bytes(b'native release fake - no DMR')
        self.sanitize.write_bytes(b'native asan fake - no DMR')
        self.gate = FakeGate()

    def write(self, relative, data):
        p = self.root / relative
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data)

    def make(self, api=None, **kwargs):
        return matrix.build_matrix(self.manifest, self.root, self.release,
                                   self.sanitize, gate=self.gate, api=api or FakeAPI(), **kwargs)

    def test_mock_bridge_full_matrix_40_and_distinct_binary_hashes(self):
        fake = FakeAPI()
        result = self.make(fake)
        self.assertEqual(result['status'], 'PASS')
        self.assertEqual(len(result['native_replay_matrix']), 40)
        self.assertEqual(len(fake.calls), 40)
        self.assertEqual(len(result['source_integrity']), 4)
        self.assertNotEqual(result['builds']['release']['identity']['sha256'],
                            result['builds']['asan_ubsan']['identity']['sha256'])

    def test_missing_real_capture_preflight_blocks_before_any_decoder_call(self):
        (self.root / json.loads(self.manifest.read_text())['files'][0]['path']).unlink()
        fake = FakeAPI()
        result = self.make(fake)
        self.assertEqual(result['status'], 'BLOCKED')
        self.assertEqual(len(result['native_replay_matrix']), 40)
        self.assertFalse(fake.calls)
        self.assertTrue(all(x['metrics'] is None for x in result['native_replay_matrix']))

    def test_missing_asan_build_is_blocked_no_partial_pass(self):
        self.sanitize.unlink()
        fake = FakeAPI()
        result = self.make(fake)
        self.assertEqual(result['status'], 'BLOCKED')
        self.assertFalse(fake.calls)

    def test_chunk_event_change_causes_fail(self):
        def tweak(result, fid, chunk):
            if fid == matrix.IDS[2] and chunk == 7:
                result['metrics']['event_trace_sha256'] = '0' * 64
        result = self.make(FakeAPI(tweak))
        self.assertEqual(result['status'], 'FAIL')
        self.assertEqual(next(r for r in result['native_replay_matrix'] if r['fixture'] == matrix.IDS[2] and r['chunk_samples'] == 7)['chunk_consistency'], 'FAIL')

    def test_native_false_identity_from_negative_fails(self):
        def tweak(result, fid, chunk):
            if fid in matrix.IDS[2:]:
                result['metrics']['false_identity_detections'] = 1
                result['metrics']['source_ids'] = [42]
        self.assertEqual(self.make(FakeAPI(tweak))['status'], 'FAIL')

    def test_second_positive_must_recover_valid_id(self):
        def tweak(result, fid, chunk):
            if fid == matrix.IDS[1]:
                result['metrics']['source_ids'] = []
                result['metrics']['talkgroups'] = []
        result = self.make(FakeAPI(tweak))
        self.assertEqual(result['status'], 'FAIL')

    def test_input_tamper_before_decode_causes_no_calls(self):
        p = self.root / json.loads(self.manifest.read_text())['files'][3]['path']
        data = bytearray(p.read_bytes())
        data[0] ^= 4
        p.write_bytes(data)
        fake = FakeAPI()
        result = self.make(fake)
        self.assertEqual(result['status'], 'FAIL')
        self.assertFalse(fake.calls)

    def test_input_sha_changes_during_replay_fail(self):
        def tweak(result, fid, chunk):
            if fid == matrix.IDS[0]:
                result['sha256_streamed'] = '0'*64
        self.assertEqual(self.make(FakeAPI(tweak))['status'], 'FAIL')

    def test_consumed_sample_count_mismatch_fail(self):
        def tweak(result, fid, chunk):
            if fid == matrix.IDS[1]:
                result['metrics']['bridge_samples_consumed'] -= 1
        self.assertEqual(self.make(FakeAPI(tweak))['status'], 'FAIL')

    def test_false_sync_count_measured_without_fake_identity(self):
        def tweak(result, fid, chunk):
            if fid == matrix.IDS[2]:
                result['metrics']['sync_total'] = 2
                result['metrics']['sync']['direct_ts1_data'] = 2
        report = self.make(FakeAPI(tweak))
        self.assertEqual(report['status'], 'PASS')
        noise_row = next(r for r in report['native_replay_matrix'] if r['fixture'] == matrix.IDS[2])
        self.assertEqual(noise_row['metrics']['false_sync_events_on_non_dmr_control'], 2)
        self.assertEqual(noise_row['metrics']['false_identity_detections'], 0)
        self.assertIsNone(noise_row['metrics']['total_decoder_rejected_candidates'])

    def test_wrong_chunk_order_rejected(self):
        report = self.make(FakeAPI(), chunks=(1, 7, 257, 4096, 1024))
        self.assertEqual(report['status'], 'FAIL')
        self.assertEqual(report['native_replay_matrix'], [])

    def test_rejected_count_not_fabricated(self):
        result = self.make(FakeAPI())
        sample = result['native_replay_matrix'][0]['metrics']
        self.assertEqual(sample['emitted_bursts_with_failed_fec_or_check'], 0)
        self.assertIsNone(sample['total_decoder_rejected_candidates'])

    def test_external_evidence_path_guard(self):
        with self.assertRaises(ValueError):
            matrix._external_path(matrix.REPO_ROOT / 'build' / 'report.json', (matrix.REPO_ROOT, self.root))
        with self.assertRaises(ValueError):
            matrix._external_path(self.root / 'secret.json', (matrix.REPO_ROOT, self.root))



class D1NParserIntegrationTests(unittest.TestCase):
    """Runs against the *actual* existing acceptance parser in a full checkout."""

    def test_rejected_emitted_bursts_reported_not_internal_drops(self):
        try:
            import acceptance
        except ImportError:
            self.skipTest('existing acceptance.py absent in isolated transfer sandbox')
        schema = 'orcsdr-dmr-d1-bridge-event-v1'
        events = [
            {'schema': schema, 'type': 'sync', 'sample_offset': 1,
             'pattern': 'direct_ts1_data'},
            {'schema': schema, 'type': 'burst', 'sample_offset': 1, 'kind': 'data',
             'slot': 1, 'color_code': 1, 'fec': 'fail', 'check': 'unknown',
             'check_kind': 'none', 'identity_valid': False, 'identity': None},
            {'schema': schema, 'type': 'sync', 'sample_offset': 3,
             'pattern': 'direct_ts1_data'},
            {'schema': schema, 'type': 'burst', 'sample_offset': 3, 'kind': 'data',
             'slot': 1, 'color_code': 1, 'fec': 'pass', 'check': 'fail',
             'check_kind': 'rs12_9', 'identity_valid': False, 'identity': None},
            {'schema': schema, 'type': 'summary', 'sample_offset': 4,
             'consumed_samples': 5},
        ]
        summary = acceptance.aggregate_events([json.dumps(e) for e in events], 5,
                                              negative=True, upstream={})
        self.assertEqual(summary['emitted_rejected_bursts'], 2)
        self.assertEqual(summary['false_identity_detections'], 0)
        self.assertEqual(summary['sync_total'], 2)
        self.assertEqual(summary['check_and_fec_valid'], 0)


if __name__ == '__main__':
    unittest.main()
