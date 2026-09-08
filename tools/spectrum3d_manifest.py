"""Record native build inputs and artifacts; never access a device."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build', required=True, type=Path)
    parser.add_argument('--prepare', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    app = root / 'apps/orcsdr-tab5'
    build = args.build.resolve()
    manifest = build / 'spectrum3d-manifest.json'
    if args.prepare:
        inputs = {}
        for folder in [app / name for name in ('ui', 'main', 'tools', 'managed_components')] + [root / 'components']:
            for path in sorted(folder.rglob('*')):
                if path.is_file() and path.suffix in ('.cpp', '.c', '.h', '.hpp', '.cmake', '.txt', '.yml', '.patch', '.ps1'):
                    inputs[path.relative_to(root).as_posix()] = sha(path)
        for path in (app / 'CMakeLists.txt', app / 'dependencies.lock', app / 'sdkconfig.defaults', build / 'sdkconfig', Path(__file__)):
            inputs[path.name if path.parent == build else path.relative_to(root).as_posix()] = sha(path)
        digest = hashlib.sha256(json.dumps(inputs, sort_keys=True).encode()).hexdigest()
        git = lambda *cmd: subprocess.check_output(['git', '-C', str(root), *cmd], text=True).strip()
        config = (build / 'sdkconfig').read_text()
        mode = next((line.split('=')[0] for line in config.splitlines()
                     if line.startswith('CONFIG_COMPILER_OPTIMIZATION_') and line.endswith('=y')), 'unknown')
        data = dict(renderer='terrain-recovery-1', git_sha=git('rev-parse', 'HEAD'),
                    dirty=bool(git('status', '--porcelain', '--untracked-files=no')), digest=digest,
                    build_mode=mode, inputs=inputs, completed_frame_identity='unavailable')
        idf = os.environ.get('IDF_PATH')
        if idf:
            data['idf_path'] = idf
            data['idf_sha'] = subprocess.check_output(['git', '-C', idf, 'rev-parse', 'HEAD'], text=True).strip()
            data['idf_dpi_sha256'] = sha(Path(idf) / 'components/esp_lcd/dsi/esp_lcd_panel_dpi.c')
        header = '#pragma once\n' + '\n'.join(
            f'#define SPECTRUM3D_{key.upper()} {json.dumps(value)}'
            for key, value in data.items() if key in ('renderer', 'git_sha', 'digest', 'build_mode'))
        header += f'\n#define SPECTRUM3D_DIRTY {int(data["dirty"])}\n'
        target = build / 'spectrum3d_build.h'
        if not target.exists() or target.read_text() != header:
            target.write_text(header)
    else:
        data = json.loads(manifest.read_text())
        data['artifacts'] = {p.name: dict(sha256=sha(p), bytes=p.stat().st_size)
                             for p in sorted(build.iterdir()) if p.suffix in ('.elf', '.bin', '.map')}
        for name in ('compile_commands.json', 'CMakeCache.txt', 'build.ninja'):
            path = build / name
            if path.exists():
                data.setdefault('build_files', {})[name] = sha(path)
    manifest.write_text(json.dumps(data, indent=2) + '\n')


if __name__ == '__main__':
    main()
