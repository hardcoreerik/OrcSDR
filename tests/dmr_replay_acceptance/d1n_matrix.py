#!/usr/bin/env python3
"""D1-N four-fixture matrix: real D1 bridge, 5 chunks, Release + sanitizers.

No binary IQ is committed or logged. The only decoder is the existing native
components/orcsdr_dmr/d1_integration/dmr_d1_replay_bridge executable.
A missing fixture or executable is BLOCKED, never a zero-false-positive PASS.
"""
from __future__ import annotations

import argparse
import hashlib
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import sys
from typing import Any

import negative_control_repro as control

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[1]
IDS = ('dmr-directmode-keyup', 'dmr-directmode-second-ptt',
       'auto-negative-noise', 'auto-negative-am')
CHUNKS = (1, 7, 257, 1024, 4096)
EXPECTED_POSITIVE = {'source_id': 3024109, 'talkgroup': 99, 'color_code': 1}
SCHEMA = 'orcsdr-dmr-d1n-four-fixture-matrix-v1'


def _gate():
    # Deferred so independent reproduction works without a checked-out repo.
    import real_iq_gate
    return real_iq_gate


def _api():
    import acceptance
    return acceptance


def digest_summary(metrics: dict, negative: bool) -> dict:
    """Retain measured receiver metrics; absent counters are *null*, not zero."""
    def counter(name: str):
        return metrics[name] if name in metrics else None

    return {'event_trace_sha256': counter('event_trace_sha256'),
            'samples_consumed': counter('bridge_samples_consumed'),
            'sync_total': counter('sync_total'),
            'sync_by_pattern': counter('sync'),
            'burst_events': counter('bursts'),
            'fec': counter('fec'), 'check': counter('check'),
            'crc16': counter('crc'), 'rs12_9': counter('rs'),
            'embedded_lc': counter('embedded_lc'),
            'fec_and_integrity_valid_bursts': counter('check_and_fec_valid'),
            'color_codes': counter('color_codes'),
            'slots_observed': counter('slots_observed'),
            'source_ids': counter('source_ids'),
            'talkgroups': counter('talkgroups'),
            'false_identity_detections': counter('false_identity_detections') if negative else None,
            'false_sync_events_on_non_dmr_control': counter('sync_total') if negative else None,
            'unverified_identity_claims': counter('unverified_identity_claims'),
            'emitted_bursts_with_failed_fec_or_check': counter('emitted_rejected_bursts'),
            'total_decoder_rejected_candidates': None,
            'rejected_candidate_count_limitation':
                'JSONL v1 only exposes emitted sync/burst events, not internal discarded candidates'}


def _assess_positive(measured: dict, base: dict) -> dict:
    issues = list(base.get('issues', []))
    reviews = list(base.get('upstream_disagreements', []))
    # The two captures were decoded independently on public CI. These values
    # are *acceptance criteria*, not injected into the native receiver.
    for key, observed in (('source_ids', EXPECTED_POSITIVE['source_id']),
                          ('talkgroups', EXPECTED_POSITIVE['talkgroup']),
                          ('color_codes', EXPECTED_POSITIVE['color_code'])):
        if observed not in measured.get(key, []):
            issues.append(f'recorded-IQ checked {key} missing required {observed}')
    if not measured.get('check_and_fec_valid_bursts', measured.get('check_and_fec_valid', 0)):
        issues.append('no FEC-plus-integrity validated burst')
    return {'status': 'FAIL' if issues else 'REVIEW' if reviews else 'PASS',
            'issues': issues, 'upstream_disagreements': reviews}


def _assess_negative(measured: dict) -> dict:
    issues = []
    for key in ('false_identity_detections', 'unverified_identity_claims'):
        if measured.get(key):
            issues.append(f'negative control reported {key}={measured[key]}')
    for key in ('source_ids', 'talkgroups'):
        if measured.get(key):
            issues.append(f'negative control produced nonempty {key}')
    return {'status': 'FAIL' if issues else 'PASS', 'issues': issues}


def _preflight_without_d1r_module(manifest: Path, root: Path) -> tuple[dict, list[dict]]:
    """Fail-closed manifest/local-byte audit if D1-R Python is not mounted.

    Used to publish a truthful BLOCKED matrix, NEVER to enable native replay.
    """
    doc = json.loads(manifest.read_text(encoding='utf-8'))
    if (doc.get('schema') != 'orcsdr-digital-voice-replay-fixtures-v1' or
            doc.get('fixture_repository') != control.SOURCE_RECIPE[0] or
            doc.get('fixture_branch') != control.SOURCE_RECIPE[1]):
        raise ValueError('unexpected manifest provenance')
    rows = {r['id']: r for r in doc['files']}
    statuses = []
    resolved_root = root.resolve()
    for fid in IDS:
        row = rows[fid]
        rel = row.get('path')
        if (not isinstance(rel, str) or not rel or rel.startswith('/') or
                '\\' in rel or any(part in ('.', '..', '') for part in rel.split('/'))):
            statuses.append({'id': fid, 'status': 'invalid', 'reason': 'unsafe source path'})
            continue
        source = (resolved_root / rel).resolve()
        if not source.is_relative_to(resolved_root):
            statuses.append({'id': fid, 'status': 'invalid', 'reason': 'source escapes root'})
            continue
        if not source.is_file():
            statuses.append({'id': fid, 'status': 'missing', 'reason': 'local private IQ not mounted'})
            continue
        expected_bytes = row['bytes']
        if source.stat().st_size != expected_bytes:
            statuses.append({'id': fid, 'status': 'mismatch', 'reason': 'size differs from manifest'})
            continue
        # Read bounded blocks; no IQ bytes enter a persisted report.
        gitsha = hashlib.sha1(f'blob {expected_bytes}\0'.encode('ascii'))
        sha = hashlib.sha256()
        with source.open('rb') as f:
            for block in iter(lambda: f.read(1024 * 1024), b''):
                gitsha.update(block)
                sha.update(block)
        verified = gitsha.hexdigest() == row['git_blob_sha1'] and (
            row.get('sha256') is None or sha.hexdigest() == row['sha256'])
        statuses.append({'id': fid, 'status': 'verified' if verified else 'mismatch',
                         'samples': expected_bytes // 4, 'observed_sha256': sha.hexdigest(),
                         'observed_git_blob_sha1': gitsha.hexdigest()})
    return doc, statuses


def build_matrix(manifest: Path, root: Path,
                 release: Path | None, sanitizer: Path | None,
                 chunks: tuple[int, ...] = CHUNKS, timeout: int = 240,
                 *, gate=None, api=None) -> dict:
    """Run all four pinned fixtures or report a truthful, complete blocked matrix.

    gate and api injections are ONLY for self-contained harness tests. Normal
    CLI always loads the real existing modules and native executable.
    """
    report: dict[str, Any] = {'schema': SCHEMA, 'status': 'BLOCKED',
        'measurement_class': 'preflight_only',
        'fixture_order': list(IDS), 'chunk_sizes': list(chunks),
        'build_modes': ['release', 'asan_ubsan'],
        'native_replay_matrix': [], 'source_integrity': [],
        'builds': {}, 'limitations': [
            'Metadata expectations are independent from actual OrcSDR decoded outputs',
            'JSONL v1 lacks a counter for all internally rejected sync/burst candidates',
            'Host decoder does not establish ESP32-P4 RF or memory performance'],
        'expected_recovered_identity_from_prior_public_real_iq_ci':
            {**EXPECTED_POSITIVE, 'evidence_origin': 'previous separate public native CI; not a measurement in this run'},
        'source_pin_status': 'not_checked'}
    if (tuple(chunks) != CHUNKS):
        report['reason'] = 'D1-N requires exactly chunk sizes 1,7,257,1024,4096 in order'
        report['status'] = 'FAIL'
        return report
    if not 1 <= timeout <= 600:
        report['status'], report['reason'] = 'FAIL', 'timeout must be in 1..600 seconds'
        return report
    builds = {'release': release, 'asan_ubsan': sanitizer}
    try:
        negative = control.reproduce(manifest)
        report['negative_reproduction'] = negative
        if negative['status'] != 'PASS':
            report['status'], report['reason'] = 'FAIL', 'pinned negative control regeneration mismatches manifest'
            return report
        if gate is None:
            try:
                gate = _gate()
            except ImportError:
                meta, statuses = _preflight_without_d1r_module(manifest, root)
                report['source_integrity'] = statuses
                report['source_provenance'] = {'repository': meta['fixture_repository'],
                    'branch': meta['fixture_branch'], 'source_commit': meta['source_commit']}
                for fid in IDS:
                    for mode in builds:
                        for chunk in chunks:
                            report['native_replay_matrix'].append({
                                'fixture': fid, 'build': mode, 'chunk_samples': chunk,
                                'status': 'BLOCKED', 'metrics': None,
                                'reason': 'D1-R host module unavailable; no native decoder started'})
                report['status'] = 'BLOCKED' if all(s['status'] in ('verified', 'missing')
                    for s in statuses) else 'FAIL'
                report['reason'] = ('D1-R Python module not mounted; '
                                    'source preflight performed, native replay not run')
                return report
        meta, rows = gate.read_manifest(manifest)
        report['source_provenance'] = {'repository': meta.get('fixture_repository'),
                                       'branch': meta.get('fixture_branch'),
                                       'source_commit': meta.get('source_commit')}
        missing = []
        invalid = []
        for fid in IDS:
            v = gate.verify_local(root, rows[fid])
            report['source_integrity'].append(v)
            if v['status'] == 'missing':
                missing.append(fid)
            elif v['status'] != 'verified':
                invalid.append(fid)
        if invalid:
            report['status'], report['reason'] = 'FAIL', 'source checksum or format mismatch: ' + ', '.join(invalid)
        elif missing:
            report['reason'] = 'pinned IQ missing from native execution filesystem: ' + ', '.join(missing)
        else:
            report['source_pin_status'] = 'all_four_verified'
        for mode, binary in builds.items():
            try:
                report['builds'][mode] = {'status': 'available', 'binary': str(binary),
                    'identity': gate.bridge_identity(binary) if binary is not None else None}
                if binary is None:
                    report['builds'][mode]['status'] = 'missing'
            except (OSError, Exception) as exc:
                report['builds'][mode] = {'status': 'missing', 'reason': str(exc), 'binary': str(binary)}
        blocked = bool(missing or invalid or any(x.get('status') != 'available' for x in report['builds'].values()))
        # Every requested combination is present, even if no native decode was possible.
        # This avoids accidentally treating absent metrics as counted zero.
        if blocked:
            for fid in IDS:
                for mode in builds:
                    for chunk in chunks:
                        report['native_replay_matrix'].append({'fixture': fid, 'build': mode,
                            'chunk_samples': chunk, 'status': 'BLOCKED' if not invalid else 'NOT_RUN',
                            'reason': 'four-fixture source and native binary preflight not satisfied',
                            'metrics': None})
            if not invalid:
                report['status'] = 'BLOCKED'
                if not missing:
                    report['reason'] = 'native Release and/or ASan/UBSan replay executable unavailable'
            return report
        if api is None:
            api = _api()
        any_fail = False
        any_review = False
        for fid in IDS:
            row = rows[fid]
            source = next(x for x in report['source_integrity'] if x['id'] == fid)
            for mode, binary in builds.items():
                outputs = []
                for chunk in chunks:
                    item: dict[str, Any] = {'fixture': fid, 'build': mode, 'chunk_samples': chunk,
                                            'status': 'NOT_RUN', 'metrics': None}
                    try:
                        measured = api.run_bridge(root, row, [str(binary.resolve())],
                                                  chunk_samples=chunk, timeout_s=timeout)
                        if (measured.get('iq_samples_streamed') != source['samples'] or
                            measured.get('sha256_streamed') != source['observed_sha256']):
                            raise ValueError('native IQ stream sample count or digest differs from preflight')
                        metrics = gate.metrics_comparable(measured['metrics'])
                        if metrics['bridge_samples_consumed'] != source['samples']:
                            raise ValueError('native bridge did not acknowledge all source samples')
                        item['status'], item['metrics'] = 'MEASURED', digest_summary(metrics, fid in IDS[2:])
                        # For selected audit fields not part of older comparable v1 schema,
                        # preserve them when present without pretending unsupported values.
                        if 'emitted_rejected_bursts' in measured['metrics']:
                            item['metrics']['emitted_bursts_with_failed_fec_or_check'] = measured['metrics']['emitted_rejected_bursts']
                        outputs.append((metrics, item))
                    except Exception as exc:
                        item['status'], item['error'] = 'FAIL', str(exc)
                        any_fail = True
                    report['native_replay_matrix'].append(item)
                if len(outputs) == len(chunks):
                    uniform = all(v[0] == outputs[0][0] for v in outputs[1:])
                    if not uniform:
                        any_fail = True
                    base_metrics = outputs[0][0]
                    assessment = _assess_negative(base_metrics) if fid in IDS[2:] else \
                        _assess_positive(base_metrics, gate.assess_fixture(fid, base_metrics, row.get('upstream_expected', {})))
                    if not uniform:
                        assessment['status'] = 'FAIL'
                        assessment['issues'].append('ordered event SHA-256 and/or decoded counters differ across chunks')
                    for _, item in outputs:
                        item['chunk_consistency'] = 'PASS' if uniform else 'FAIL'
                        item['fixture_assessment'] = assessment
                    any_fail |= assessment['status'] == 'FAIL'
                    any_review |= assessment['status'] == 'REVIEW'
        report['status'] = 'FAIL' if any_fail else 'REVIEW' if any_review else 'PASS'
        report['measurement_class'] = 'actual_native_dmr_bridge_four_verified_source_files'
        report['reason'] = ('native D1 replay executed; strict per-capture and cross-chunk checks applied'
                            if not any_fail else 'at least one native evidence or consistency gate failed')
        return report
    except Exception as exc:
        report['status'] = 'BLOCKED'
        report['reason'] = f'preflight or native harness unavailable: {exc}'
        return report


def _external_path(path: Path, roots: tuple[Path, ...]) -> Path:
    p = path.resolve()
    for root in roots:
        root = root.resolve()
        if p == root or root in p.parents:
            raise ValueError(f'reports/logs must not be placed beneath {root}')
    return p


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, default=control.MANIFEST)
    parser.add_argument('--fixtures-root', required=True, type=Path)
    parser.add_argument('--release-bridge', type=Path)
    parser.add_argument('--sanitizer-bridge', type=Path)
    parser.add_argument('--chunk-sizes', nargs='+', type=int, default=list(CHUNKS))
    parser.add_argument('--timeout', type=int, default=240)
    parser.add_argument('--report', required=True, type=Path)
    parser.add_argument('--log', required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        report_path = _external_path(args.report, (REPO_ROOT, args.fixtures_root))
        log_path = _external_path(args.log, (REPO_ROOT, args.fixtures_root))
        if report_path == log_path:
            raise ValueError('report and log paths must differ')
        report = build_matrix(args.manifest, args.fixtures_root, args.release_bridge,
                              args.sanitizer_bridge, tuple(args.chunk_sizes), args.timeout)
        report['execution_utc'] = datetime.now(timezone.utc).isoformat()
        report['runtime_report_file'] = str(report_path)
        report['runtime_log_file'] = str(log_path)
        report_path.parent.mkdir(parents=True, exist_ok=True)
        log_path.parent.mkdir(parents=True, exist_ok=True)
        with report_path.open('x', encoding='utf-8') as fp:
            json.dump(report, fp, indent=2, sort_keys=True)
            fp.write('\n')
        with log_path.open('x', encoding='utf-8') as fp:
            fp.write('D1-N native four-fixture matrix\n')
            fp.write(f'Status: {report["status"]}\n')
            fp.write(f'Run UTC: {report["execution_utc"]}\n')
            fp.write(f'Report: {report_path}\n')
            fp.write(f'Fixture directory: {args.fixtures_root}\n')
            fp.write(f'Chunk sizes: {list(args.chunk_sizes)}\n')
            fp.write(f'Reason: {report.get("reason", "none")}\n')
            for f in report.get('source_integrity', []):
                fp.write(f'Source {f["id"]}: {f["status"]}\n')
            for mode, v in report.get('builds', {}).items():
                fp.write(f'Bridge {mode}: {v["status"]}\n')
            for item in report['native_replay_matrix']:
                fp.write(f'{item["fixture"]} {item["build"]} chunk={item["chunk_samples"]}: {item["status"]}\n')
        print(json.dumps({'status': report['status'], 'reason': report.get('reason'),
                          'matrix_rows': len(report['native_replay_matrix']),
                          'report': str(report_path), 'log': str(log_path)}, indent=2))
        return {'PASS': 0, 'REVIEW': 2, 'FAIL': 2, 'BLOCKED': 3}[report['status']]
    except (ValueError, OSError) as exc:
        print(f'D1-N cannot save external evidence: {exc}', file=sys.stderr)
        return 2

if __name__ == '__main__':
    raise SystemExit(main())
