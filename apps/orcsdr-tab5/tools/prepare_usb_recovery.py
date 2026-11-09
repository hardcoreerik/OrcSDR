"""Prepare a verified ESP-IDF USB override inside one OrcSDR build directory."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shutil
import subprocess

BASE_HASHES = {
    'hub.c': 'dcffde3a7856fe84e5070cbf2bb3ecf76f200bfaeb49725f210cbfd7e7c426d3',
    'usb_host.c': '90c363001ce460d2d1648db89f2db4a7aab126446ce2f6840e27c462a009f83f',
    'CMakeLists.txt': '9d1795dec7d80f1bc003505ddb0cd81be52d71364ab3335fd3a29677051baba4',
}


def digest(path):
    return hashlib.sha256(path.read_bytes().replace(b'\r\n', b'\n')).hexdigest()


def prepare(idf, build):
    """Return the component directory after checking source and generated identities."""
    version = (idf / 'tools/cmake/version.cmake').read_text()
    parts = [re.search(r'set\(IDF_VERSION_' + part + r' (\d+)\)', version)
             for part in ('MAJOR', 'MINOR', 'PATCH')]
    if any(part is None for part in parts) or [part.group(1) for part in parts] != ['5', '5', '4']:
        raise RuntimeError('OrcSDR USB recovery requires ESP-IDF 5.5.4')
    source = idf / 'components/usb'
    for name, expected in BASE_HASHES.items():
        if digest(source / name) != expected:
            raise RuntimeError(f'Unexpected SDK USB source: {name}. Use stock ESP-IDF 5.5.4.')
    patch = Path(__file__).resolve().parent / 'patches/esp-idf-5.5.4-usb-root-recovery.patch'
    signature = digest(patch)
    folder = build.resolve() / ('orcsdr-usb-' + signature[:16])
    component = folder / 'usb'
    marker = folder / 'verified.json'
    if folder.exists():
        if not marker.is_file():
            raise RuntimeError(f'Incomplete USB override at {folder}. Use a fresh build directory.')
        saved = json.loads(marker.read_text())
        actual = {str(p.relative_to(component)): digest(p) for p in component.rglob('*') if p.is_file()}
        if saved != {'patch': signature, 'files': actual}:
            raise RuntimeError(f'Modified USB override at {folder}. Use a fresh build directory.')
        return component
    folder.mkdir(parents=True)
    shutil.copytree(source, component, ignore=shutil.ignore_patterns('test_apps', 'host_test', '.git'))
    subprocess.run(['git', 'apply', '--check', str(patch)], cwd=component, check=True, capture_output=True)
    subprocess.run(['git', 'apply', str(patch)], cwd=component, check=True, capture_output=True)
    hashes = {str(p.relative_to(component)): digest(p) for p in component.rglob('*') if p.is_file()}
    marker.write_text(json.dumps({'patch': signature, 'files': hashes}, indent=2), encoding='utf-8')
    return component


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--idf', type=Path, required=True)
    parser.add_argument('--build', type=Path, required=True)
    options = parser.parse_args()
    try:
        print(prepare(options.idf.resolve(), options.build).as_posix())
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'USB recovery preparation failed: {error}\n')
