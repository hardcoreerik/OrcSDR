"""Report aggregate ULS table shapes without extracting or printing records."""
import argparse
import hashlib
import json
import zipfile
from collections import Counter
from pathlib import Path


def audit(path, tables):
    path = Path(path)
    with path.open('rb') as source:
        digest = hashlib.file_digest(source, 'sha256').hexdigest()
    results = {}
    with zipfile.ZipFile(path) as archive:
        for table in tables:
            matches = [entry for entry in archive.infolist() if entry.filename.casefold() == (table + '.dat').casefold()]
            if len(matches) != 1:
                raise ValueError(f'{table}: expected exactly one root table')
            widths = Counter()
            wrong_type = rows = 0
            with archive.open(matches[0]) as source:
                for line in source:
                    fields = line.rstrip(b'\r\n').split(b'|')
                    rows += 1
                    widths[len(fields)] += 1
                    wrong_type += fields[0] != table.encode('ascii')
            results[table] = {'rows': rows, 'wrong_type': wrong_type,
                              'widths': {str(k): widths[k] for k in sorted(widths)}}
    return {'archive_bytes': path.stat().st_size, 'sha256': digest, 'tables': results}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('tables', nargs='+', choices=('HD', 'EN', 'LO', 'AN', 'FR', 'EM'))
    args = parser.parse_args()
    print(json.dumps(audit(args.archive, args.tables), sort_keys=True, indent=2))
