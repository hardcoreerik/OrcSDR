"""Finalize an exact-tag M5Burner build with the reviewed embedded world archive.

No device access, network access, firmware compilation, signing or publication.
Run with the ESP-IDF Python environment after build-m5burner.ps1.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile

WORLD_SHA = '9aea08772bacf1f024d1da90cc52aa8fcf0b0e37405415dc7c91e75e56596f0f'

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--world', type=Path, required=True)
    args = parser.parse_args()
    repo = args.repo.resolve()
    def git(*parts):
        return subprocess.check_output(['git','-C',str(repo),*parts],text=True).strip()
    assert git('describe','--tags','--exact-match') == args.version, 'Not exact tag'
    assert not git('status','--porcelain','--untracked-files=no'), 'Tracked tree is dirty'
    assert sha(args.world) == WORLD_SHA and args.world.stat().st_size <= 0x100000
    assert args.world.read_bytes()[:8] == b'PMTiles\x03'
    bundle = repo / 'dist' / ('OrcSDR-Tab5-' + args.version)
    build = repo / 'apps/orcsdr-tab5/build-native-hosted3'
    manifest = json.loads((bundle/'m5burner-upload.json').read_text(encoding='utf-8-sig'))
    app = build/'orcsdr_tab5.bin'
    assert args.version.encode() in app.read_bytes(), 'App does not carry release version'
    assert manifest['version'] == args.version.lstrip('v')
    c6 = bundle/manifest['c6_firmware']
    assert sha(c6) == manifest['c6_sha256']
    assert c6.read_bytes() in app.read_bytes(), 'C6 embedding mismatch'
    elf = build/'orcsdr_tab5.elf'
    assert b'https://github.com/hardcoreerik/OrcMaps/releases/download/maps-catalog-v1' in elf.read_bytes()
    parts = [(0x2000,build/'bootloader/bootloader.bin'),
             (0x8000,build/'partition_table/partition-table.bin'),
             (0x10000,app),(0x650000,args.world.resolve())]
    assert parts[1][1].read_bytes().find(b'orcmaps_world') >= 0, 'World partition absent'
    assert app.stat().st_size <= 0x600000-0x40000
    image = bundle/manifest['firmware']
    with tempfile.TemporaryDirectory(prefix='orcsdr-map-package-') as temporary:
        merged = Path(temporary)/'merged.bin'
        subprocess.run([sys.executable,'-m','esptool','--chip','esp32p4','merge_bin',
                        '--output',str(merged),'--flash_mode','dio','--flash_freq','80m',
                        '--flash_size','16MB',*[value for offset,path in parts for value in (hex(offset),str(path))]],check=True)
        data = merged.read_bytes()
        for offset,path in parts:
            assert data[offset:offset+path.stat().st_size] == path.read_bytes()
        assert data[0x9000:0xf000] == b'\xff'*0x6000, 'Merged image NVS is not blank'
        image.write_bytes(data)
    local = bundle/'local-m5burner'
    fw = local/'firmware'
    for name,path in [('bootloader_0x2000.bin',parts[0][1]),
                      ('partition-table_0x8000.bin',parts[1][1]),
                      ('orcsdr_tab5_0x10000.bin',app)]:
        (fw/name).write_bytes(path.read_bytes())
    world_name = 'orcmaps_world_0x650000.pmtiles'
    (fw/world_name).write_bytes(args.world.read_bytes())
    script = (fw/'flash.sh').read_text()
    script = script.rstrip() + ' \\\n0x650000 ' + world_name + '\n'
    # A previous invocation must not append the region twice.
    assert script.count('0x650000 ') == 1, 'Already finalized; do not duplicate world region'
    (fw/'flash.sh').write_bytes(script.encode('utf-8'))
    local_manifest = json.loads((local/'m5burner.json').read_text(encoding='utf-8-sig'))
    local_manifest['description'] = 'OrcSDR for Tab5 with offline world map. Local update preserves NVS; full merged M5Burner install resets settings. SD card is untouched.'
    local_manifest['embedded_world'] = {'offset':'0x650000','sha256':WORLD_SHA,'file':'firmware/'+world_name}
    (local/'m5burner.json').write_text(json.dumps(local_manifest,indent=2),encoding='utf-8')
    manifest.update(source_commit=git('rev-parse','HEAD'),sha256=sha(image),
                    embedded_world={'offset':'0x650000','sha256':WORLD_SHA,'bytes':args.world.stat().st_size},
                    production_catalog='https://github.com/hardcoreerik/OrcMaps/releases/download/maps-catalog-v1')
    (bundle/'m5burner-upload.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    (bundle/'README.txt').write_text(
        f'OrcSDR {args.version} for M5Stack Tab5\n\n'
        'Merged .bin: flash at address 0; saved NVS settings reset; setup opens. SD card untouched.\n'
        'Local M5Burner ZIP: code and world-map regions only; existing NVS settings preserved.\n'
        'C6 image is embedded for an optional confirmed Settings > Firmware & Updates action; packaging does not flash C6.\n'
        'The 543 MB global regional pack is an optional in-app download, not included in this firmware.\n'
        f'Firmware SHA-256: {sha(image)}\n',encoding='utf-8')
    archive = bundle/f'OrcSDR-Tab5-{args.version}-local-m5burner.zip'
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as out:
        for path in sorted(local.rglob('*')):
            if path.is_file():out.write(path,path.relative_to(local).as_posix())
    with zipfile.ZipFile(archive) as check:
        assert check.read('firmware/'+world_name) == args.world.read_bytes()
        assert check.read('firmware/orcsdr_tab5_0x10000.bin') == app.read_bytes()
        assert check.read('firmware/flash.sh') == (fw/'flash.sh').read_bytes()
    provenance = dict(manifest,app_sha256=sha(app),elf_sha256=sha(elf),
                      local_zip_sha256=sha(archive),finalizer_sha256=sha(Path(__file__)),
                      exact_region_and_blank_nvs_checks=True,hardware_flashed=False)
    (bundle/'package-provenance.json').write_text(json.dumps(provenance,indent=2),encoding='utf-8')
    checksum_paths = [image,archive,bundle/'RELEASE_NOTES.txt',bundle/'package-provenance.json']
    (bundle/'SHA256SUMS.txt').write_text(''.join(f'{sha(path)} *{path.name}\n' for path in checksum_paths),encoding='ascii')
    print('MAPS_M5BURNER_FINALIZED '+str(bundle))

if __name__ == '__main__':
    main()
