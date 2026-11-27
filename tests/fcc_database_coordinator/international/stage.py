"""Private host-only AU/JP technical frequency projection; not a national adapter."""
import argparse
import csv
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import re
import sqlite3
import tempfile
import zipfile
from decimal import Decimal, localcontext

U64 = (1 << 64) - 1
UNITS = {'Hz': 1, 'kHz': 1000, 'MHz': 1000000, 'GHz': 1000000000}


def digest(path):
    with Path(path).open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def inventory(path, max_members=10000, max_bytes=100 << 30):
    """Report archive member metadata, never row values; reject ambiguous paths."""
    members = []
    seen = set()
    total = 0
    with zipfile.ZipFile(path) as z:
        if len(z.infolist()) > max_members:
            raise ValueError('member limit')
        for m in z.infolist():
            name = m.filename.replace('\\', '/')
            p = PurePosixPath(name)
            key = str(p).casefold()
            if p.is_absolute() or '..' in p.parts or ':' in name or key in seen:
                raise ValueError('unsafe or duplicate member')
            seen.add(key)
            if m.flag_bits & 1 or (m.external_attr >> 16) & 0o170000 == 0o120000:
                raise ValueError('encrypted or symlink member')
            total += m.file_size
            if total > max_bytes:
                raise ValueError('expanded size limit')
            if m.is_dir():
                continue
            h = hashlib.sha256()
            with z.open(m) as f:
                for chunk in iter(lambda: f.read(1 << 20), b''):
                    h.update(chunk)
            members.append({'member': name, 'bytes': m.file_size, 'sha256': h.hexdigest()})
    return {'archive_sha256': digest(path), 'members': members, 'expanded_bytes': total,
            'qualification': 'private_inventory_only'}


def hz(value, unit):
    if not re.fullmatch(r'[0-9]{1,20}(?:\.[0-9]{1,12})?', value):
        raise ValueError('invalid frequency')
    with localcontext() as ctx:
        ctx.prec = 50
        n = Decimal(value) * UNITS[unit]
    if n <= 0 or n > U64 or n != n.to_integral_value():
        raise ValueError('unrepresentable frequency')
    return int(n)


def project(stream, output, *, country, source, key_columns, low_column, high_column=None, unit, approved_schema):
    """Explicit schema mapping only; private output contains numeric triples, no names or locations."""
    if country not in ('AU', 'JP') or not approved_schema or unit not in UNITS:
        raise ValueError('reviewed country/schema/unit required')
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,64}', source) or not key_columns or len(set(key_columns)) != len(key_columns):
        raise ValueError('invalid source/key mapping')
    out = Path(output)
    if out.exists():
        raise FileExistsError(out)
    counts = dict(input=0, indexed=0, excluded_frequency=0, discrete=0, intervals=0)
    with tempfile.TemporaryDirectory(dir=out.parent) as work:
        db = sqlite3.connect(Path(work)/'sort.sqlite')
        try:
            db.execute('CREATE TABLE rows (id TEXT PRIMARY KEY, lo TEXT, hi TEXT) WITHOUT ROWID')
            reader = csv.reader(stream, strict=True)
            header = next(reader, [])
            required = list(key_columns) + [low_column] + ([high_column] if high_column else [])
            if len(header) != len(set(header)) or any(c not in header for c in required):
                raise ValueError('schema mismatch')
            cols = [header.index(c) for c in key_columns]
            lo_idx = header.index(low_column)
            hi_idx = header.index(high_column) if high_column else None
            for row in reader:
                counts['input'] += 1
                if len(row) != len(header) or any(not row[i] or len(row[i]) > 4096 for i in cols):
                    raise ValueError(f'invalid row/key at record {counts["input"]}')
                identity = json.dumps([country, source, [row[i] for i in cols]], ensure_ascii=False, separators=(',', ':')).encode()
                ident = int.from_bytes(hashlib.sha256(identity).digest()[:8], 'big')
                if not ident:
                    raise ValueError('zero experimental id')
                low = high = None
                try:
                    low = hz(row[lo_idx], unit)
                    high = hz(row[hi_idx], unit) if hi_idx is not None else low
                    if high < low:
                        raise ValueError('reversed interval')
                except ValueError:
                    low = high = None
                    counts['excluded_frequency'] += 1
                try:
                    db.execute('INSERT INTO rows VALUES (?,?,?)', (f'{ident:016x}', None if low is None else str(low), None if high is None else str(high)))
                except sqlite3.IntegrityError as e:
                    raise ValueError('duplicate key or experimental id collision') from e
                if low is not None:
                    counts['indexed'] += 1
                    counts['discrete' if low == high else 'intervals'] += 1
            db.commit()
            counts['unique_discrete'] = db.execute('SELECT count(DISTINCT lo) FROM rows WHERE lo=hi').fetchone()[0]
            tmp = Path(work)/'projection.jsonl'
            with tmp.open('w', encoding='utf-8', newline='\n') as f:
                for ident, low, high in db.execute('SELECT id,lo,hi FROM rows WHERE lo IS NOT NULL ORDER BY id'):
                    f.write(json.dumps({'id': int(ident, 16), 'low': int(low), 'high': int(high)}, separators=(',', ':'))+'\n')
            # Exclusive destination creation, even if another process races.
            import os
            os.link(tmp, out)
        finally:
            db.close()
    return dict(counts, country=country, source=source, projection_sha256=digest(out),
                qualification='private_unsigned_experimental_not_for_distribution')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='action', required=True)
    i = sub.add_parser('inventory'); i.add_argument('archive')
    s = sub.add_parser('project'); s.add_argument('csv'); s.add_argument('output')
    s.add_argument('--country', choices=['AU','JP'], required=True)
    s.add_argument('--source', required=True); s.add_argument('--key', action='append', required=True)
    s.add_argument('--low', required=True); s.add_argument('--high'); s.add_argument('--unit', choices=UNITS, required=True)
    s.add_argument('--encoding', default='utf-8-sig'); s.add_argument('--reviewed-schema', action='store_true', required=True)
    a = p.parse_args()
    if a.action == 'inventory':
        result = inventory(a.archive)
    else:
        before = digest(a.csv)
        with open(a.csv, encoding=a.encoding, newline='') as f:
            result = project(f, a.output, country=a.country, source=a.source, key_columns=a.key, low_column=a.low, high_column=a.high, unit=a.unit, approved_schema=a.reviewed_schema)
        if digest(a.csv) != before:
            Path(a.output).unlink()
            raise ValueError('input changed during projection')
        result['input_sha256'] = before
    print(json.dumps(result, sort_keys=True, ensure_ascii=False))


if __name__ == '__main__':
    main()
