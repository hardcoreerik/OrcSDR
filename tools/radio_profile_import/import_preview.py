#!/usr/bin/env python3
"""Offline, read-only import preview of portable radio-channel profiles.

No operation in this module writes or modifies a user radio configuration.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
import re
import sys
from collections import Counter
from pathlib import Path
from typing import Any

SCHEMA = 'orcsdr.radio-profiles'
VERSION = 1
MAX_BYTES = 8 * 1024 * 1024
MAX_ROWS = 50000
MIN_HZ = 24_000_000
MAX_HZ = 1_766_000_000

# Only explicitly supported attributes enter identity/content comparison.
FIELDS = {
    'frequency_hz', 'frequency_mhz', 'protocol', 'role', 'modulation',
    'name', 'region', 'network_id', 'site_id', 'color_code', 'nac', 'ran',
    'slot', 'talkgroup_id', 'radio_id', 'tx_frequency_hz', 'notes',
    'scan_list', 'skip', 'hold',
}
IDENTITY_FIELDS = ('region', 'network_id', 'site_id', 'color_code', 'nac',
                   'ran', 'slot', 'talkgroup_id', 'radio_id')
NUMBER_FIELDS = {'color_code': (0, 15), 'nac': (0, 4095), 'ran': (0, 63),
                 'slot': (1, 2), 'talkgroup_id': (1, 16777215),
                 'radio_id': (1, 16777215)}
BOOL_FIELDS = {'skip', 'hold'}
TEXT_FIELDS = {'name', 'region', 'network_id', 'site_id', 'notes', 'scan_list'}
PROTOCOLS = {'DMR', 'P25', 'NXDN', 'FM', 'NFM', 'WFM', 'AM', 'OTHER'}
ROLES = {'unknown', 'repeater', 'simplex', 'control', 'traffic', 'base', 'mobile'}

# Each alias is (canonical field, frequency unit when applicable).
ALIASES = {
    'frequency': 'frequency_mhz', 'freq': 'frequency_mhz',
    'rx frequency': 'frequency_mhz', 'receive frequency': 'frequency_mhz',
    'frequency mhz': 'frequency_mhz', 'rx freq': 'frequency_mhz',
    'rx freq mhz': 'frequency_mhz', 'frequency hz': 'frequency_hz',
    'rx frequency hz': 'frequency_hz', 'receive frequency hz': 'frequency_hz',
    'name': 'name', 'channel name': 'name', 'channel': 'name',
    'protocol': 'protocol', 'mode': 'protocol', 'type': 'protocol',
    'role': 'role', 'modulation': 'modulation',
    'region': 'region', 'location': 'region', 'area': 'region',
    'network id': 'network_id', 'network': 'network_id',
    'site': 'site_id', 'site id': 'site_id',
    'color code': 'color_code', 'colour code': 'color_code', 'cc': 'color_code',
    'nac': 'nac', 'ran': 'ran', 'time slot': 'slot', 'timeslot': 'slot',
    'slot': 'slot', 'ts': 'slot', 'tg': 'talkgroup_id', 'talkgroup': 'talkgroup_id',
    'talkgroup id': 'talkgroup_id', 'radio id': 'radio_id',
    'tx frequency hz': 'tx_frequency_hz', 'transmit frequency hz': 'tx_frequency_hz',
    'notes': 'notes', 'comment': 'notes', 'scan list': 'scan_list',
    'skip': 'skip', 'hold': 'hold',
}


class ImportErrorDetail(ValueError):
    """Input is unusable as a whole; no import plan may be trusted."""


def header_key(text: str) -> str:
    return re.sub(r'[^a-z0-9]+', ' ', text.casefold()).strip()


def canonical_column(text: str) -> str | None:
    if text in FIELDS:
        return text
    return ALIASES.get(header_key(text))


def load_bytes(path: Path) -> tuple[bytes, str]:
    try:
        if path.stat().st_size > MAX_BYTES:
            raise ImportErrorDetail(f'{path}: input exceeds {MAX_BYTES} bytes')
        data = path.read_bytes()
        if len(data) > MAX_BYTES:
            raise ImportErrorDetail(f'{path}: input exceeds {MAX_BYTES} bytes')
        text = data.decode('utf-8-sig')
    except (OSError, UnicodeError) as exc:
        raise ImportErrorDetail(f'{path}: cannot read UTF-8 input: {exc}') from exc
    return data, text


def overrides(specs: list[str]) -> dict[str, str]:
    result: dict[str, str] = {}
    for spec in specs:
        if '=' not in spec:
            raise ImportErrorDetail(f'bad --map {spec!r}: expected FIELD=COLUMN')
        field, column = spec.split('=', 1)
        if field not in FIELDS or not field or not column:
            raise ImportErrorDetail(f'bad --map {spec!r}: unknown target or empty column')
        if field in result:
            raise ImportErrorDetail(f'duplicate --map target: {field}')
        result[field] = column
    if 'frequency_hz' in result and 'frequency_mhz' in result:
        raise ImportErrorDetail('map frequency_hz or frequency_mhz, not both')
    return result


def normalize_mapping(headers: list[str], custom: dict[str, str]) -> tuple[dict[str, str], list[str]]:
    if not headers or any(not h or not h.strip() for h in headers):
        raise ImportErrorDetail('CSV has an empty column header')
    if len(headers) != len(set(headers)) or len(headers) != len(set(h.casefold() for h in headers)):
        raise ImportErrorDetail('CSV has duplicate column headers')
    mapped: dict[str, str] = {}
    for header in headers:
        field = canonical_column(header)
        if field:
            if field in mapped:
                raise ImportErrorDetail(f'ambiguous CSV columns mapping to {field!r}; specify a unique file layout')
            mapped[field] = header
    for field, header in custom.items():
        if header not in headers:
            raise ImportErrorDetail(f'--map {field}={header}: column not found')
        # An explicit override may take a column away from its default field.
        for earlier, assigned in list(mapped.items()):
            if assigned == header or earlier == field:
                del mapped[earlier]
        mapped[field] = header
    if 'frequency_hz' in mapped and 'frequency_mhz' in mapped:
        raise ImportErrorDetail('ambiguous CSV frequency fields: select a single receive-frequency unit')
    unknown = sorted(h for h in headers if h not in mapped.values())
    return mapped, unknown


def read_source(path: Path, mapping: dict[str, str] | None = None) -> dict[str, Any]:
    data, text = load_bytes(path)
    digest = hashlib.sha256(data).hexdigest()
    source: dict[str, Any] = {'filename': path.name, 'sha256': digest,
                              'kind': 'imported_file', 'evidence': 'directory_candidate'}
    rows: list[dict[str, Any]] = []
    warnings: list[str] = []
    suffix = path.suffix.lower()
    if suffix == '.csv':
        try:
            reader = csv.DictReader(io.StringIO(text, newline=''), restkey='__EXTRA__')
            headers = reader.fieldnames or []
            mapped, unknown = normalize_mapping(headers, mapping or {})
            if not mapped.get('frequency_hz') and not mapped.get('frequency_mhz'):
                raise ImportErrorDetail('CSV needs a frequency column; use --map frequency_mhz=COL or frequency_hz=COL')
            if unknown:
                warnings.append('unsupported CSV columns retained in source data, not imported: ' + ', '.join(unknown))
            for i, raw in enumerate(reader, 1):
                if i > MAX_ROWS:
                    raise ImportErrorDetail(f'CSV exceeds {MAX_ROWS} rows')
                fields = {field: raw.get(header) for field, header in mapped.items()}
                entry = {'index': i, 'line': reader.line_num, 'fields': fields, 'raw': raw}
                if raw.get('__EXTRA__') is not None:
                    entry['structural_error'] = 'CSV row has more cells than column headers'
                rows.append(entry)
        except csv.Error as exc:
            raise ImportErrorDetail(f'CSV parsing failed: {exc}') from exc
    elif suffix == '.json':
        if mapping:
            raise ImportErrorDetail('--map is only supported for CSV')
        try:
            obj = json.loads(text, parse_constant=lambda s: (_ for _ in ()).throw(ValueError(s)))
        except (ValueError, json.JSONDecodeError) as exc:
            raise ImportErrorDetail(f'invalid JSON: {exc}') from exc
        if not isinstance(obj, dict) or obj.get('schema') != SCHEMA or type(obj.get('version')) is not int or obj['version'] != VERSION:
            raise ImportErrorDetail(f'unsupported JSON schema/version: expected {SCHEMA} v{VERSION}')
        if not isinstance(obj.get('profiles'), list) or len(obj['profiles']) > MAX_ROWS:
            raise ImportErrorDetail(f'JSON profiles must be an array of at most {MAX_ROWS} rows')
        metadata = obj.get('source', {})
        if not isinstance(metadata, dict):
            raise ImportErrorDetail('JSON source must be an object')
        for key in ('name', 'url', 'license', 'retrieved_at'):
            if key in metadata:
                if not isinstance(metadata[key], str):
                    raise ImportErrorDetail(f'JSON source.{key} must be a string')
                source['asserted_' + key] = metadata[key]
        unknown_meta = sorted(set(metadata) - {'name', 'url', 'license', 'retrieved_at'})
        if unknown_meta:
            warnings.append('unrecognized JSON source metadata: ' + ', '.join(unknown_meta))
        for i, raw in enumerate(obj['profiles'], 1):
            if not isinstance(raw, dict):
                rows.append({'index': i, 'line': None, 'fields': {}, 'raw': raw,
                             'structural_error': 'profile must be an object'})
            else:
                rows.append({'index': i, 'line': None, 'fields': raw, 'raw': raw})
    else:
        raise ImportErrorDetail('supported input extensions are .csv and .json')
    return {'source': source, 'rows': rows, 'warnings': warnings}


def integer(value: Any, field: str, low: int, high: int) -> int:
    if isinstance(value, bool) or not re.fullmatch(r'[0-9]+', str(value).strip()):
        raise ValueError(f'{field} must be a decimal integer')
    number = int(str(value).strip())
    if not low <= number <= high:
        raise ValueError(f'{field} must be in {low}..{high}')
    return number


def frequency(value: Any, field: str) -> int:
    if isinstance(value, bool):
        raise ValueError(f'{field} is not a frequency')
    text = str(value).strip().replace('_', '')
    match = re.fullmatch(r'([0-9]+(?:\.[0-9]+)?)\s*(hz|mhz)?', text, re.IGNORECASE)
    if not match:
        raise ValueError(f'{field} must be a positive frequency in Hz or MHz')
    number, suffix = match.groups()
    unit = (suffix or ('mhz' if field == 'frequency_mhz' else 'hz')).lower()
    from decimal import Decimal
    hz = Decimal(number) * (1_000_000 if unit == 'mhz' else 1)
    if hz != hz.to_integral_value() or not 1 <= hz <= 100_000_000_000:
        raise ValueError(f'{field} must resolve to an integral positive number of Hz')
    return int(hz)


def normalize_row(row: dict[str, Any], min_hz: int, max_hz: int) -> tuple[dict[str, Any], list[str], list[str]]:
    raw = row['fields']
    normalized: dict[str, Any] = {}
    errors: list[str] = []
    warnings: list[str] = []
    if 'structural_error' in row:
        return {}, [row['structural_error']], []
    extra = sorted(set(raw) - FIELDS)
    if extra:
        warnings.append('unsupported fields ignored: ' + ', '.join(extra))
    if raw.get('frequency_hz') not in (None, '') and raw.get('frequency_mhz') not in (None, ''):
        errors.append('both frequency_hz and frequency_mhz supplied')
    else:
        field = 'frequency_hz' if raw.get('frequency_hz') not in (None, '') else 'frequency_mhz'
        if raw.get(field) in (None, ''):
            errors.append('missing receive frequency')
        else:
            try:
                normalized['frequency_hz'] = frequency(raw[field], field)
                if not min_hz <= normalized['frequency_hz'] <= max_hz:
                    warnings.append(f'outside configured receiver range {min_hz}..{max_hz} Hz; not proof of unsupported hardware')
            except ValueError as exc:
                errors.append(str(exc))
    protocol = str(raw.get('protocol') or '').strip().upper()
    protocol_aliases = {'DIGITAL': 'OTHER', 'NARROW FM': 'NFM', 'P25 PHASE I': 'P25'}
    protocol = protocol_aliases.get(protocol, protocol)
    if not protocol:
        errors.append('missing protocol/mode; use --map protocol=COLUMN or add protocol field')
    elif protocol not in PROTOCOLS:
        errors.append(f'unsupported protocol {protocol!r}')
    else:
        normalized['protocol'] = protocol
    role = str(raw.get('role') or 'unknown').strip().casefold()
    if role not in ROLES:
        errors.append(f'unsupported role {role!r}')
    else:
        normalized['role'] = role
    for field in TEXT_FIELDS | {'modulation'}:
        value = raw.get(field)
        if value is None or value == '':
            continue
        if not isinstance(value, (str, int)) or isinstance(value, bool):
            errors.append(f'{field} must be text')
            continue
        cleaned = ' '.join(str(value).strip().split())
        if len(cleaned) > 240:
            errors.append(f'{field} exceeds 240 characters')
        elif cleaned:
            normalized[field] = cleaned
    for field, (low, high) in NUMBER_FIELDS.items():
        value = raw.get(field)
        if value is None or value == '':
            continue
        try:
            normalized[field] = integer(value, field, low, high)
        except ValueError as exc:
            errors.append(str(exc))
    if raw.get('tx_frequency_hz') not in (None, ''):
        try:
            normalized['tx_frequency_hz'] = frequency(raw['tx_frequency_hz'], 'tx_frequency_hz')
        except ValueError as exc:
            errors.append(str(exc))
    for field in BOOL_FIELDS:
        value = raw.get(field)
        if value is None or value == '':
            continue
        if type(value) is bool:
            normalized[field] = value
        elif str(value).strip().casefold() in ('true', 'yes', '1', 'false', 'no', '0'):
            normalized[field] = str(value).strip().casefold() in ('true', 'yes', '1')
        else:
            errors.append(f'{field} must be true/false, yes/no or 1/0')
    if 'color_code' in normalized and protocol != 'DMR':
        errors.append('color_code is only applicable to DMR')
    if 'slot' in normalized and protocol != 'DMR':
        errors.append('slot is only applicable to DMR')
    if 'nac' in normalized and protocol != 'P25':
        errors.append('nac is only applicable to P25')
    if 'ran' in normalized and protocol != 'NXDN':
        errors.append('ran is only applicable to NXDN')
    # User/directory names are not over-the-air evidence.
    return normalized, errors, warnings


def identity(record: dict[str, Any]) -> tuple:
    base = (record['protocol'], record['role'], record['frequency_hz'])
    specifics = tuple((key, str(record[key]).casefold()) for key in IDENTITY_FIELDS if key in record)
    return base + (specifics,)


def content_key(record: dict[str, Any]) -> str:
    # Independent of column order, file name and source attribution.
    encoded = json.dumps(record, sort_keys=True, ensure_ascii=False, separators=(',', ':'))
    return hashlib.sha256(encoded.encode('utf-8')).hexdigest()


def partial_overlap(a: dict[str, Any], b: dict[str, Any]) -> bool:
    if (a['protocol'], a['frequency_hz']) != (b['protocol'], b['frequency_hz']):
        return False
    if a['role'] != b['role'] and 'unknown' not in (a['role'], b['role']):
        return False
    if identity(a) == identity(b):
        return False
    # Explicitly different identifiers establish separate systems/sites.
    for field in IDENTITY_FIELDS:
        if field in a and field in b and str(a[field]).casefold() != str(b[field]).casefold():
            return False
    # At least one identity discriminator is unknown on one side.
    return True


def parse_selection(value: str, size: int) -> set[int]:
    if value == 'all':
        return set(range(1, size + 1))
    selected: set[int] = set()
    for part in value.split(','):
        match = re.fullmatch(r'([1-9]\d*)(?:-([1-9]\d*))?', part.strip())
        if not match:
            raise ImportErrorDetail(f'bad --select segment: {part!r}')
        a, b = int(match[1]), int(match[2] or match[1])
        if a > b or b > size:
            raise ImportErrorDetail(f'--select range outside 1..{size}: {part!r}')
        selected.update(range(a, b + 1))
    return selected


def parse_renames(values: list[str], size: int) -> dict[int, str]:
    names: dict[int, str] = {}
    for entry in values:
        if '=' not in entry:
            raise ImportErrorDetail('--rename requires INDEX=NEW_NAME')
        index, name = entry.split('=', 1)
        if not re.fullmatch(r'[1-9]\d*', index) or not 1 <= int(index) <= size:
            raise ImportErrorDetail(f'--rename row outside 1..{size}: {index}')
        name = ' '.join(name.strip().split())
        if not name or len(name) > 240 or int(index) in names:
            raise ImportErrorDetail(f'invalid/duplicate --rename for {index}')
        names[int(index)] = name
    return names


def build_preview(imported: dict[str, Any], existing: dict[str, Any] | None = None,
                  *, selected: str = 'all', on_conflict: str = 'skip',
                  on_review: str = 'skip', renames: list[str] | None = None,
                  min_hz: int = MIN_HZ, max_hz: int = MAX_HZ) -> dict[str, Any]:
    if min_hz < 1 or max_hz < min_hz:
        raise ImportErrorDetail('invalid receiver range')
    baseline: list[dict[str, Any]] = []
    for row in (existing or {}).get('rows', []):
        profile, errors, _ = normalize_row(row, min_hz, max_hz)
        if errors:
            raise ImportErrorDetail(f'existing snapshot row {row["index"]} invalid: {"; ".join(errors)}')
        baseline.append(profile)
    incoming = imported['rows']
    choices = parse_selection(selected, len(incoming))
    names = parse_renames(renames or [], len(incoming))
    results: list[dict[str, Any]] = []
    plan: list[dict[str, Any]] = []
    # Do not compare against a generated plan: sources are classified against
    # immutable baseline plus all preceding valid input rows, independently of selection.
    seen: list[tuple[str, int, dict[str, Any]]] = [('existing', i + 1, p) for i, p in enumerate(baseline)]
    for row in incoming:
        record, errors, warnings = normalize_row(row, min_hz, max_hz)
        decision: dict[str, Any] = {'index': row['index'], 'source_line': row['line'],
                                    'status': 'invalid' if errors else 'new',
                                    'errors': errors, 'warnings': warnings,
                                    'matched': [], 'selected': row['index'] in choices,
                                    'action': 'skip', 'record': record if not errors else None}
        if not errors:
            hits = [(src, idx, other) for src, idx, other in seen if identity(record) == identity(other)]
            if hits:
                same = any(content_key(record) == content_key(other) for _, _, other in hits)
                decision['status'] = 'duplicate' if same else 'conflict'
                decision['matched'] = [{'scope': scope, 'index': ix} for scope, ix, _ in hits]
            else:
                overlaps = [(scope, ix) for scope, ix, other in seen if partial_overlap(record, other)]
                if overlaps:
                    decision['status'] = 'review'
                    decision['matched'] = [{'scope': scope, 'index': ix} for scope, ix in overlaps]
            seen.append(('incoming', row['index'], record))
            if row['index'] in choices:
                state = decision['status']
                if state == 'new' or (state == 'conflict' and on_conflict == 'keep-both') or (state == 'review' and on_review == 'keep-both'):
                    chosen = dict(record)
                    if row['index'] in names:
                        chosen['name'] = names[row['index']]
                    decision['action'] = 'create_separate'
                    plan.append({'index': row['index'], 'action': 'create_separate',
                                 'record': chosen,
                                 'provenance': {**imported['source'], 'source_row': row['index'],
                                                'source_line': row['line'],
                                                'claim': 'imported_profile_not_rf_observation'}})
                elif state == 'duplicate':
                    decision['warnings'].append('duplicate suppressed; never replace an existing record')
                elif state in ('review', 'conflict'):
                    decision['warnings'].append('requires explicit keep-both opt-in; no existing record updated')
        results.append(decision)
    counts = dict(sorted(Counter(r['status'] for r in results).items()))
    for status in ('new', 'duplicate', 'conflict', 'review', 'invalid'):
        counts.setdefault(status, 0)
    return {'schema': 'orcsdr.radio-import-preview', 'version': 1,
            'dry_run': True, 'writes_existing_configuration': False,
            'source': imported['source'],
            'existing': (existing or {}).get('source'),
            'warnings': imported['warnings'],
            'counts': counts, 'rows': results,
            'plan': {'creates': plan, 'create_count': len(plan),
                     'updates': [], 'deletes': [], 'requires_separate_commit': True}}


def print_text(preview: dict[str, Any]) -> str:
    summary = preview['counts']
    lines = [f"OFFLINE PREVIEW — {preview['source']['filename']}",
             'Source SHA-256: ' + preview['source']['sha256'],
             'No radio configurations were written.',
             'Rows: ' + ', '.join(f'{kind}={summary[kind]}' for kind in ('new', 'duplicate', 'conflict', 'review', 'invalid')),
             f"Planned creates: {preview['plan']['create_count']} (not applied)"]
    for warn in preview['warnings']:
        lines.append('WARNING: ' + warn)
    for row in preview['rows']:
        match = ', '.join(f"{m['scope']}#{m['index']}" for m in row['matched'])
        lines.append(f"#{row['index']} [{row['status']}] selected={row['selected']} action={row['action']}" +
                     (f' matches={match}' if match else ''))
        lines.extend('  ERROR: ' + msg for msg in row['errors'])
        lines.extend('  WARNING: ' + msg for msg in row['warnings'])
    return '\n'.join(lines) + '\n'


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path, help='CSV channel list or versioned OrcSDR JSON')
    parser.add_argument('--existing', type=Path, help='read-only OrcSDR JSON baseline snapshot')
    parser.add_argument('--map', action='append', default=[], metavar='FIELD=COLUMN', help='CSV field mapping override')
    parser.add_argument('--select', default='all', metavar='1,3-5', help='1-based input row indexes to plan (default all)')
    parser.add_argument('--on-conflict', choices=['skip', 'keep-both'], default='skip')
    parser.add_argument('--on-review', choices=['skip', 'keep-both'], default='skip')
    parser.add_argument('--rename', action='append', default=[], metavar='INDEX=NAME')
    parser.add_argument('--min-hz', type=int, default=MIN_HZ)
    parser.add_argument('--max-hz', type=int, default=MAX_HZ)
    parser.add_argument('--source-name', help='asserted source/provider name for CSV or JSON')
    parser.add_argument('--source-url', help='asserted original data URL (never fetched)')
    parser.add_argument('--source-license', help='asserted license/terms')
    parser.add_argument('--source-retrieved-at', help='asserted source retrieval date')
    parser.add_argument('--format', choices=['text', 'json'], default='text')
    parser.add_argument('--output', type=Path, help='create a NEW preview-only file (exclusive; no overwrite)')
    args = parser.parse_args(argv)
    try:
        imported = read_source(args.input, overrides(args.map))
        for key, value in (('name', args.source_name), ('url', args.source_url),
                           ('license', args.source_license),
                           ('retrieved_at', args.source_retrieved_at)):
            if value is not None:
                imported['source']['asserted_' + key] = value
        existing = read_source(args.existing) if args.existing else None
        if existing is not None and args.existing.suffix.lower() != '.json':
            raise ImportErrorDetail('--existing requires a versioned OrcSDR JSON file')
        preview = build_preview(imported, existing, selected=args.select,
                                on_conflict=args.on_conflict, on_review=args.on_review,
                                renames=args.rename, min_hz=args.min_hz, max_hz=args.max_hz)
        rendered = (json.dumps(preview, indent=2, ensure_ascii=False, sort_keys=True) + '\n'
                    if args.format == 'json' else print_text(preview))
        if args.output:
            if args.output.resolve() in {args.input.resolve(), *( [args.existing.resolve()] if args.existing else [] )}:
                raise ImportErrorDetail('output cannot be an input or existing snapshot')
            try:
                with args.output.open('x', encoding='utf-8') as f:
                    f.write(rendered)
            except OSError as exc:
                raise ImportErrorDetail(f'cannot create preview output (never overwrite): {exc}') from exc
        else:
            sys.stdout.write(rendered)
        return 0
    except ImportErrorDetail as exc:
        print(f'import-preview: {exc}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())