"""Build a private experimental exact-frequency index from one ULS FR table.

This is not an installable database: postings are source row numbers, not stations.
"""
import argparse
import hashlib
import json
import os
import re
import sqlite3
import struct
import tempfile
import zipfile
from decimal import Decimal
from pathlib import Path


def hz(value):
    if not re.fullmatch(rb'[0-9]{1,8}(?:\.[0-9]{1,8})?', value):
        raise ValueError('invalid MHz')
    number = Decimal(value.decode('ascii')) * 1000000
    if number <= 0 or number != number.to_integral_value() or number > 0x7fffffffffffffff:
        raise ValueError('frequency is not a positive integral Hz')
    return int(number)


def build(lines, destination):
    destination = Path(destination)
    if destination.exists(): raise FileExistsError(destination)
    report = dict(rows=0, indexed=0, intervals=0, invalid=0, frequencies=0, max_postings=0)
    with tempfile.TemporaryDirectory(dir=destination.parent) as work:
        db = sqlite3.connect(Path(work) / 'sort.sqlite')
        try:
            db.execute('CREATE TABLE postings (frequency INTEGER NOT NULL, row_number INTEGER NOT NULL)')
            for line in lines:
                fields = line.rstrip(b'\r\n').split(b'|')
                if len(fields) != 30 or fields[0] != b'FR': raise ValueError('unexpected FR schema')
                report['rows'] += 1
                if report['rows'] > 0xffffffff: raise ValueError('too many source rows')
                try:
                    low = hz(fields[10].strip())
                    high = hz(fields[11].strip()) if fields[11].strip() else low
                    if high < low: raise ValueError('reversed range')
                except ValueError:
                    report['invalid'] += 1
                    continue
                if high != low:
                    report['intervals'] += 1
                    continue
                db.execute('INSERT INTO postings VALUES (?, ?)', (low, report['rows']))
                report['indexed'] += 1
            db.execute('CREATE INDEX by_frequency ON postings(frequency, row_number)')
            db.commit()
            report['frequencies'] = db.execute('SELECT count(DISTINCT frequency) FROM postings').fetchone()[0]
            temporary = Path(work) / 'index'
            with temporary.open('wb') as out:
                out.write(struct.pack('<8sII', b'FCCEXP01', report['frequencies'], report['indexed']))
                offset = 0
                for frequency, count in db.execute('SELECT frequency, count(*) FROM postings GROUP BY frequency ORDER BY frequency'):
                    out.write(struct.pack('<QII', frequency, offset, count))
                    offset += count
                    report['max_postings'] = max(report['max_postings'], count)
                for (number,) in db.execute('SELECT row_number FROM postings ORDER BY frequency, row_number'):
                    out.write(struct.pack('<I', number))
            report['bytes'] = temporary.stat().st_size
            with temporary.open('rb') as source: report['sha256'] = hashlib.file_digest(source, 'sha256').hexdigest()
            os.link(temporary, destination)
            return report
        finally:
            db.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    with zipfile.ZipFile(args.archive) as archive:
        matches = [entry for entry in archive.infolist() if entry.filename.casefold() == 'fr.dat']
        if len(matches) != 1: raise ValueError('expected exactly one root FR.dat')
        with archive.open(matches[0]) as source:
            print(json.dumps(build(source, args.output), sort_keys=True, indent=2))
