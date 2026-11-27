"""Research-only archive acquisition and integrity inventory; no package ABI."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import tempfile
from datetime import datetime, timezone
from urllib.parse import urlsplit
from urllib.request import urlopen
import zipfile

CHUNK = 1 << 20


def inspect_archive(path, max_expanded):
    members = []
    expanded = 0
    with zipfile.ZipFile(path) as archive:
        seen = set()
        for info in archive.infolist():
            name = info.filename
            parts = name.replace('\\', '/').split('/')
            if name.startswith(('/', '\\')) or ':' in parts[0] or '..' in parts or name in seen:
                raise ValueError('unsafe or duplicate member: ' + name)
            seen.add(name)
            if info.flag_bits & 1:
                raise ValueError('encrypted member: ' + name)
            expanded += info.file_size
            if expanded > max_expanded:
                raise ValueError('expanded size limit exceeded')
            if info.is_dir():
                continue
            digest = hashlib.sha256()
            size = 0
            with archive.open(info) as stream:
                while chunk := stream.read(CHUNK):
                    size += len(chunk)
                    if size > info.file_size:
                        raise ValueError('member exceeds declared size')
                    digest.update(chunk)
            if size != info.file_size:
                raise ValueError('member size mismatch')
            members.append(dict(name=name, bytes=size, sha256=digest.hexdigest()))
    if not members:
        raise ValueError('archive contains no files')
    return expanded, members


def acquire(source, output, url=None, local=None, expected=None,
            max_download=20 << 30, max_expanded=100 << 30):
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_-]*', source):
        raise ValueError('invalid source identifier')
    if (url is None) == (local is None):
        raise ValueError('choose exactly one input')
    if url and urlsplit(url).scheme != 'https':
        raise ValueError('HTTPS required')
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    digest = hashlib.sha256()
    size = 0
    with tempfile.TemporaryDirectory(dir=output) as work:
        tmp = Path(work) / 'archive.zip'
        with (urlopen(url, timeout=60) if url else open(local, 'rb')) as stream, tmp.open('wb') as dest:
            if url and urlsplit(stream.geturl()).scheme != 'https':
                raise ValueError('HTTPS redirect required')
            headers = dict(stream.headers.items()) if url else {}
            final_url = stream.geturl() if url else None
            while chunk := stream.read(CHUNK):
                size += len(chunk)
                if size > max_download:
                    raise ValueError('download size limit exceeded')
                digest.update(chunk)
                dest.write(chunk)
        sha = digest.hexdigest()
        if expected and sha != expected.lower():
            raise ValueError('archive SHA-256 mismatch')
        expanded, members = inspect_archive(tmp, max_expanded)
        manifest = dict(source=source, requested_url=url, final_url=final_url,
                        local_input=bool(local), received_utc=datetime.now(timezone.utc).isoformat(),
                        archive_bytes=size, archive_sha256=sha, extracted_bytes=expanded,
                        members=members, http_etag=headers.get('ETag'),
                        http_last_modified=headers.get('Last-Modified'),
                        expected_hash_checked=bool(expected), redistribution='not_reviewed')
        manifest_path = Path(work) / 'manifest.json'
        manifest_path.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
        final = output / (source + '-' + sha)
        if final.exists():
            raise FileExistsError(final)
        # Publish archive and manifest together, on the same filesystem.
        os.rename(work, final)
    return final


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source')
    p.add_argument('--output', type=Path, required=True)
    group = p.add_mutually_exclusive_group(required=True)
    group.add_argument('--url')
    group.add_argument('--local', type=Path)
    p.add_argument('--sha256')
    p.add_argument('--max-download', type=int, default=20 << 30)
    p.add_argument('--max-expanded', type=int, default=100 << 30)
    a = p.parse_args()
    print(acquire(a.source, a.output, a.url, a.local, a.sha256, a.max_download, a.max_expanded))


if __name__ == '__main__':
    main()
