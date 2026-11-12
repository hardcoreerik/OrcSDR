#!/usr/bin/env python3
"""Host-only acceptance tests using synthetic private temp IQ and a fake bridge."""
import contextlib
import hashlib
import io
import json
from pathlib import Path
import os
import time
import struct
import sys
import tempfile
import unittest

import acceptance as a

BRIDGE = r'''
import argparse
import json
import struct
import sys
p = argparse.ArgumentParser()
p.add_argument('--mode', default='valid')
p.add_argument('--input-format', required=True)
p.add_argument('--sample-rate-hz', type=int, required=True)
p.add_argument('--fixture-id', required=True)
args = p.parse_args()
assert args.input_format == 'cf32-le' and args.sample_rate_hz == 48000
buf = sys.stdin.buffer.read()
assert len(buf) % 8 == 0
samples = [x for x in struct.iter_unpack('<ff', buf)]
assert len(samples) >= 2
assert samples[0] == (0.5, -0.25)
if args.mode == 'badexit':
    sys.exit(3)
if args.mode == 'hang':
    import time
    time.sleep(20)
events = []
def event(event_type, offset, **values):
    events.append(dict(schema='orcsdr-dmr-d1-bridge-event-v1', type=event_type, sample_offset=offset, **values))
if args.mode not in ('none', 'badexit', 'crc'):
    event('sync', 0, pattern='direct_ts1_data')
    event('burst', 1, kind='data', slot=1, color_code=1, fec='pass' if args.mode != 'untrusted' else 'unknown',
          check='pass', check_kind='rs12_9', identity_valid=True, identity={'source_id': 3024109, 'talkgroup': 99})
if args.mode == 'crc':
    event('burst', 1, kind='control', slot=2, color_code=3, fec='pass',
          check='pass', check_kind='crc16', identity_valid=False, identity=None)
if args.mode == 'malformed':
    events[0]['sample_offset'] = -1
if args.mode == 'garbage':
    print('{broken')
else:
    if args.mode != 'nosummary':
        event('summary', len(samples) - 1, consumed_samples=len(samples) + (1 if args.mode == 'wrongsummary' else 0))
    for item in events:
        print(json.dumps(item))
'''


class AcceptanceTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        base = Path(self.tmp.name)
        self.root = base / 'fixtures'
        (self.root / 'DMR').mkdir(parents=True)
        self.bridge = base / 'dummy_bridge.py'
        self.bridge.write_text(BRIDGE, encoding='utf-8')
        self.samples = [(0.5, -0.25), (-1.0, 32767 / 32768)] * 32

    def fixture(self, fmt='cs16-le', fid='dmr-directmode-keyup', origin='real_air', overrides=None):
        if fmt == 'cs16-le':
            data = b''.join(struct.pack('<hh', int(i * 32768), int(q * 32768)) for i, q in self.samples)
            suffix = '.cs16'
        else:
            data = b''.join(struct.pack('<ff', i, q) for i, q in self.samples)
            suffix = '.cfile'
        row = {'id': fid, 'path': 'DMR/test' + suffix, 'capture_stage': 'channelized_iq',
               'protocol': 'DMR direct-mode', 'origin': origin, 'format': fmt, 'sample_rate_hz': 48000,
               'bytes': len(data), 'git_blob_sha1': hashlib.sha1(f'blob {len(data)}\0'.encode() + data).hexdigest(),
               'sha256': hashlib.sha256(data).hexdigest(),
               'upstream_expected': {'group_id': 99, 'source_id': 3024109, 'color_code': 1,
                                     'min_data_sync': 1, 'min_voice_sync': 1, 'min_superframes': 2}}
        if overrides:
            row.update(overrides)
        (self.root / row['path']).parent.mkdir(parents=True, exist_ok=True)
        (self.root / row['path']).write_bytes(data)
        return row

    def command(self, mode='valid'):
        return [sys.executable, str(self.bridge), '--mode', mode]

    def test_direct_metadata_from_repo_is_first_and_cs16(self):
        self.assertEqual(a.DIRECT_IDS, ('dmr-directmode-keyup', 'dmr-directmode-second-ptt'))
        self.assertEqual(a.NEGATIVE_IDS, ('auto-negative-noise', 'auto-negative-am'))
        self.assertEqual(a.choose_ids({x: {} for x in a.DIRECT_IDS + a.NEGATIVE_IDS}, None, True, False),
                         list(a.DIRECT_IDS) + list(a.NEGATIVE_IDS))

    def test_cs16_stream_hash_gate_and_evidence(self):
        row = self.fixture()
        result = a.run_bridge(self.root, row, self.command(), chunk_samples=3)
        self.assertEqual(result['iq_samples_streamed'], 64)
        self.assertTrue(result['integrity']['verified'])
        metrics = result['metrics']
        self.assertEqual(metrics['sync']['direct_ts1_data'], 1)
        self.assertEqual(metrics['check_and_fec_valid'], 1)
        self.assertEqual(metrics['slots_check_fec_bursts'], [1])
        self.assertEqual(metrics['color_codes'], [1])
        self.assertEqual(metrics['source_ids'], [3024109])
        self.assertEqual(metrics['talkgroups'], [99])
        self.assertEqual(metrics['upstream_checks']['min_voice_sync'], 'not_met')
        self.assertEqual(metrics['upstream_checks']['min_superframes'], 'not_assessed')

    def test_cf32_pass_through(self):
        row = self.fixture(fmt='cf32-le')
        result = a.run_bridge(self.root, row, self.command(), chunk_samples=1)
        self.assertEqual(result['iq_samples_streamed'], 64)
        self.assertEqual(result['integrity']['format'], 'cf32-le')
        self.assertEqual(result['metrics']['sync_total'], 1)

    def test_same_size_corruption_does_not_run_decoder(self):
        row = self.fixture()
        p = self.root / row['path']
        altered = bytearray(p.read_bytes())
        altered[5] ^= 1
        p.write_bytes(altered)
        _, evidence = a.verify(self.root, row)
        self.assertEqual(evidence['error'], 'git_blob_sha1_mismatch')
        with self.assertRaisesRegex(a.AcceptanceError, 'integrity not verified'):
            a.run_bridge(self.root, row, self.command())

    def test_wrong_length_and_wrong_sha256(self):
        row = self.fixture()
        p = self.root / row['path']
        data = p.read_bytes()
        p.write_bytes(data[:-1])
        self.assertEqual(a.verify(self.root, row)[1]['error'], 'length_mismatch')
        p.write_bytes(data)
        row['sha256'] = '0' * 64
        self.assertEqual(a.verify(self.root, row)[1]['error'], 'sha256_mismatch')

    def test_false_identity_on_negative_control(self):
        row = self.fixture(fid='auto-negative-noise', origin='deterministic_synthetic',
                           overrides={'protocol': 'UNKNOWN'})
        result = a.run_bridge(self.root, row, self.command())
        self.assertEqual(result['metrics']['false_identity_detections'], 1)
        self.assertEqual(result['metrics']['status'], 'negative_false_identity')
        no_detect = a.run_bridge(self.root, row, self.command('none'))
        self.assertEqual(no_detect['metrics']['false_identity_detections'], 0)
        self.assertEqual(no_detect['metrics']['status'], 'negative_no_valid_identity')

    def test_invalid_crc_fec_never_becomes_proven_identity(self):
        row = self.fixture()
        metrics = a.run_bridge(self.root, row, self.command('untrusted'))['metrics']
        self.assertEqual(metrics['unverified_identity_claims'], 1)
        self.assertEqual(metrics['source_ids'], [])
        self.assertEqual(metrics['talkgroups'], [])
        self.assertEqual(metrics['color_codes'], [])

    def test_malformed_bridge_events_and_nonzero_exit_rejected(self):
        row = self.fixture()
        for mode, why in [('malformed', 'sample_offset'), ('garbage', 'JSONL'), ('nosummary', 'end-of-stream'), ('wrongsummary', 'acknowledge'), ('badexit', 'failed')]:
            with self.subTest(mode=mode):
                with self.assertRaisesRegex(a.AcceptanceError, why):
                    a.run_bridge(self.root, row, self.command(mode))

    def test_event_offsets_order_and_schema(self):
        header = {'schema': 'orcsdr-dmr-d1-bridge-event-v1', 'type': 'sync', 'pattern': 'direct_ts1_data'}
        bad = [json.dumps(dict(header, sample_offset=8)), json.dumps(dict(header, sample_offset=2))]
        with self.assertRaisesRegex(a.AcceptanceError, 'unordered'):
            a.aggregate_events(bad, 64, negative=False, upstream={})
        bad[0] = json.dumps(dict(header, sample_offset=65))
        with self.assertRaisesRegex(a.AcceptanceError, 'sample_offset'):
            a.aggregate_events(bad[:1], 64, negative=False, upstream={})
        bad[0] = json.dumps(dict(header, sample_offset=0, source_id=123))
        with self.assertRaisesRegex(a.AcceptanceError, 'invalid sync'):
            a.aggregate_events(bad[:1], 64, negative=False, upstream={})

    def test_path_traversal_and_symlink_escape(self):
        for path in ('../P25/profile.cs16', '/tmp/secret.cs16', 'DMR/../other.cs16', 'DMR\\bad.cs16'):
            with self.subTest(path=path):
                with self.assertRaises(a.AcceptanceError):
                    a.safe_relative(path)
        row = self.fixture()
        external = Path(self.tmp.name) / 'outside.cs16'
        external.write_bytes((self.root / row['path']).read_bytes())
        alias = self.root / 'DMR' / 'link.cs16'
        try:
            alias.symlink_to(external)
        except OSError:
            self.skipTest('symlink unsupported on host')
        row['path'] = 'DMR/link.cs16'
        with self.assertRaisesRegex(a.AcceptanceError, 'outside'):
            a.verify(self.root, row)

    def test_manifest_selection_and_format_rejection(self):
        row = self.fixture()
        meta = {'schema': 'orcsdr-digital-voice-replay-fixtures-v1', 'fixture_repository': 'REA-Results',
                'fixture_branch': 'research', 'fixture_root': 'inbox/fixtures', 'source_commit': 'some',
                'files': [row]}
        path = Path(self.tmp.name) / 'manifest.json'
        path.write_text(json.dumps(meta), encoding='utf-8')
        self.assertEqual(a.load_metadata(path)[1][row['id']]['format'], 'cs16-le')
        meta['files'][0]['format'] = 'cf32-le'
        path.write_text(json.dumps(meta), encoding='utf-8')
        with self.assertRaisesRegex(a.AcceptanceError, 'conflicting'):
            a.load_metadata(path)
        meta['files'][0]['format'] = 'cs16-le'
        meta['files'][0]['sample_rate_hz'] = 96000
        path.write_text(json.dumps(meta), encoding='utf-8')
        with self.assertRaisesRegex(a.AcceptanceError, '48000'):
            a.load_metadata(path)

    def test_crc_and_rs_are_not_conflated(self):
        row = self.fixture()
        crc = a.run_bridge(self.root, row, self.command('crc'))['metrics']
        self.assertEqual(crc['crc']['pass'], 1)
        self.assertEqual(crc['rs']['pass'], 0)
        self.assertEqual(crc['source_ids'], [])
        rs = a.run_bridge(self.root, row, self.command())['metrics']
        self.assertEqual(rs['rs']['pass'], 1)
        self.assertEqual(rs['crc']['pass'], 0)
        self.assertEqual(rs['bridge_samples_consumed'], 64)

    def test_bridge_process_timeout(self):
        row = self.fixture()
        start = time.monotonic()
        with self.assertRaisesRegex(a.AcceptanceError, 'timeout'):
            a.run_bridge(self.root, row, self.command('hang'), timeout_s=1)
        self.assertLess(time.monotonic() - start, 6.0)

    def test_nan_cf32_rejected_even_if_hash_pinned(self):
        row = self.fixture(fmt='cf32-le')
        path = self.root / row['path']
        data = bytearray(path.read_bytes())
        data[:4] = struct.pack('<f', float('nan'))
        path.write_bytes(data)
        row['git_blob_sha1'] = hashlib.sha1(f'blob {len(data)}\0'.encode() + data).hexdigest()
        row['sha256'] = hashlib.sha256(data).hexdigest()
        self.assertTrue(a.verify(self.root, row)[1]['verified'])
        with self.assertRaisesRegex(a.AcceptanceError, 'non-finite'):
            a.run_bridge(self.root, row, self.command())

    def test_cli_list_verify_and_external_report(self):
        row = self.fixture()
        meta = {'schema': 'orcsdr-digital-voice-replay-fixtures-v1', 'fixture_repository': 'REA-Results',
                'fixture_branch': 'research', 'fixture_root': 'inbox/fixtures', 'source_commit': 'some',
                'files': [row]}
        manifest = Path(self.tmp.name) / 'manifest.json'
        manifest.write_text(json.dumps(meta), encoding='utf-8')
        destination = Path(self.tmp.name) / 'report.json'
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            rc = a.main(['list', '--manifest', str(manifest), '--id', row['id']])
        self.assertEqual(rc, 0)
        self.assertEqual(json.loads(out.getvalue())['outcome'], 'metadata_only')
        with contextlib.redirect_stdout(out := io.StringIO()):
            rc = a.main(['verify', '--manifest', str(manifest), '--id', row['id'],
                         '--fixtures-root', str(self.root)])
        self.assertEqual(rc, 0)
        self.assertEqual(json.loads(out.getvalue())['outcome'], 'verified')
        with contextlib.redirect_stdout(out := io.StringIO()):
            rc = a.main(['run', '--manifest', str(manifest), '--id', row['id'], '--fixtures-root', str(self.root),
                         '--bridge-command', ' '.join(self.command()), '--report', str(destination)])
        self.assertEqual(rc, 0)
        self.assertEqual(json.loads(destination.read_text())['outcome'], 'bridge_evidence_collected')
        self.assertEqual(json.loads(out.getvalue())['fixtures'][0]['metrics']['source_ids'], [3024109])

    def test_no_raw_iq_written_to_repo(self):
        row = self.fixture()
        before = set(Path(__file__).parent.iterdir())
        a.run_bridge(self.root, row, self.command(), chunk_samples=1)
        after = set(Path(__file__).parent.iterdir())
        self.assertEqual(before, after)


if __name__ == '__main__':
    unittest.main()
