#!/usr/bin/env python3
"""Reproduce *exact* pinned REA Digital Voice noise/AM negatives (no SDR needed).

Independent, stdlib-only transcription of private REA-Results
inbox/fixtures/digital-voice/AUTO/generate_controls.py. The manifest is
read-only and is NEVER modified to accommodate a different output.
No IQ files are written unless --write-external-root is explicitly given.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
from typing import Any

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[1]
MANIFEST = HERE.parent / 'digital_voice_replay' / 'manifest.json'
IDS = ('auto-negative-noise', 'auto-negative-am')
RATE = 48000
N = RATE * 2
MASK = 0xFFFFFFFF
SOURCE_RECIPE = ('hardcoreerik/REA-Results',
                 'chatgpt/digital-voice-rsa-rea-2026-11-01',
                 'inbox/fixtures/digital-voice/AUTO/generate_controls.py')

class ReproductionError(Exception):
    pass


def generate_noise() -> bytes:
    # Exact xorshift32 sequence, low 16 bits as signed CS16 I and Q.
    state = 0x13579BDF
    out = bytearray()
    for _ in range(N):
        state ^= (state << 13) & MASK
        state ^= state >> 17
        state ^= (state << 5) & MASK
        i = (state & 0xFFFF) - 32768
        state ^= (state << 13) & MASK
        state ^= state >> 17
        state ^= (state << 5) & MASK
        q = (state & 0xFFFF) - 32768
        out += struct.pack('<hh', i, q)
    return bytes(out)


def generate_am() -> bytes:
    # Exact 200 Hz triangular amplitude envelope, real-valued CS16 IQ.
    out = bytearray()
    for n in range(N):
        p = n % 240
        tri = p if p < 120 else 240 - p
        out += struct.pack('<hh', 8000 + 40 * tri, 0)
    return bytes(out)


def fingerprints(data: bytes) -> dict[str, Any]:
    return {
        'bytes': len(data),
        'samples': len(data) // 4 if not len(data) % 4 else None,
        'git_blob_sha1': hashlib.sha1(b'blob ' + str(len(data)).encode('ascii') + b'\0' + data).hexdigest(),
        'sha256': hashlib.sha256(data).hexdigest(),
    }


def _rows(path: Path) -> dict[str, dict]:
    doc = json.loads(path.read_text(encoding='utf-8'))
    if (doc.get('schema') != 'orcsdr-digital-voice-replay-fixtures-v1' or
            doc.get('fixture_repository') != SOURCE_RECIPE[0] or
            doc.get('fixture_branch') != SOURCE_RECIPE[1]):
        raise ReproductionError('unexpected fixture manifest provenance')
    rows = {r['id']: r for r in doc['files']}
    if len(rows) != len(doc['files']):
        raise ReproductionError('duplicate fixture ID')
    for fid in IDS:
        if fid not in rows:
            raise ReproductionError('missing pinned negative control ' + fid)
        row = rows[fid]
        if (row.get('format') != 'cs16-le' or row.get('sample_rate_hz') != RATE or
                row.get('origin') != 'deterministic_synthetic' or
                row.get('protocol') != 'UNKNOWN' or
                row.get('bytes') != N * 4 or not row.get('git_blob_sha1') or
                not row.get('sha256') or row.get('capture_stage') != 'channelized_iq' or
                not str(row.get('path', '')).startswith('AUTO/') or
                not str(row.get('path', '')).endswith('.cs16')):
            raise ReproductionError('invalid pinned negative metadata for ' + fid)
    return rows


def reproduce(manifest: Path = MANIFEST, external_root: Path | None = None,
              write: bool = False) -> dict:
    """Verify original pins without ever changing manifest or original IQ.

    Optional external_root means *compare* pre-existing recorded bytes.
    write=True explicitly generates missing files using exclusive creation,
    refuses any path inside the repo and never overwrites an existing file.
    """
    rows = _rows(manifest)
    report = {'schema': 'orcsdr-dmr-d1n-negative-reproduction-v1',
              'source_recipe': dict(zip(('repository', 'branch', 'path'), SOURCE_RECIPE)),
              'fixtures': [], 'status': 'PASS'}
    if write and external_root is None:
        raise ReproductionError('--write-external-root must be provided to write IQ')
    if external_root is not None:
        external_root = external_root.resolve()
        if external_root == REPO_ROOT or REPO_ROOT in external_root.parents:
            raise ReproductionError('private IQ output/check destination cannot be inside OrcSDR')
    for fid, generator in zip(IDS, (generate_noise, generate_am)):
        row = rows[fid]
        data = generator()
        observed = fingerprints(data)
        match = (observed['bytes'] == row['bytes'] and
                 observed['git_blob_sha1'] == row['git_blob_sha1'] and
                 observed['sha256'] == row['sha256'])
        result: dict[str, Any] = {'id': fid, 'path': row['path'],
              'regenerated': observed, 'pinned_git_blob_sha1': row['git_blob_sha1'],
              'pinned_sha256': row['sha256'], 'regenerated_hashes_match': match,
              'external_source_comparison': 'not_requested'}
        if not match:
            report['status'] = 'FAIL'
            result['reason'] = 'regeneration differs from pinned source; original pins unchanged'
        if external_root is not None:
            target = external_root / row['path']
            if target.resolve().is_relative_to(REPO_ROOT):
                raise ReproductionError('destination redirects into OrcSDR')
            if write and match:
                target.parent.mkdir(parents=True, exist_ok=True)
                # Exclusive create: never overwrite a REA capture or user data.
                with target.open('xb') as stream:
                    stream.write(data)
            if target.is_file():
                # Compare actual local bytes to *both* pinned hashes, not only generator.
                with target.open('rb') as stream:
                    original = stream.read()
                actual = fingerprints(original)
                result['external_source_comparison'] = 'match' if (
                    actual['bytes'] == row['bytes'] and
                    actual['git_blob_sha1'] == row['git_blob_sha1'] and
                    actual['sha256'] == row['sha256']) else 'mismatch'
                result['external_source_hashes'] = actual
                if result['external_source_comparison'] != 'match':
                    report['status'] = 'FAIL'
            else:
                result['external_source_comparison'] = 'missing'
                report['status'] = 'BLOCKED' if report['status'] == 'PASS' else report['status']
        report['fixtures'].append(result)
    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, default=MANIFEST)
    parser.add_argument('--external-root', type=Path, help='check existing private AUTO/ controls')
    parser.add_argument('--write-external-root', type=Path, help='create exact AUTO/ files outside repository (exclusive)')
    parser.add_argument('--report', type=Path, help='JSON output path outside OrcSDR')
    args = parser.parse_args(argv)
    try:
        if args.external_root and args.write_external_root:
            raise ReproductionError('choose either --external-root or --write-external-root')
        report = reproduce(args.manifest, args.write_external_root or args.external_root,
                           write=args.write_external_root is not None)
        if args.report:
            dest = args.report.resolve()
            if dest == REPO_ROOT or REPO_ROOT in dest.parents:
                raise ReproductionError('report must stay outside OrcSDR')
            dest.parent.mkdir(parents=True, exist_ok=True)
            with dest.open('x', encoding='utf-8') as f:
                json.dump(report, f, indent=2, sort_keys=True)
                f.write('\n')
        print(json.dumps(report, indent=2, sort_keys=True))
        return {'PASS': 0, 'FAIL': 2, 'BLOCKED': 3}[report['status']]
    except (OSError, ValueError, ReproductionError) as exc:
        print(f'D1-N negative control reproducibility error: {exc}', file=sys.stderr)
        return 2

if __name__ == '__main__':
    raise SystemExit(main())
