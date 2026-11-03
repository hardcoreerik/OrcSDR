"""Validate a build-owned C6 recovery package and generate its embedded manifest.

The expected manifest digest is supplied explicitly by the firmware builder.
The JSON's own 'trusted' flag is never a trust decision. No device access.
"""
import argparse
import hashlib
import json
from pathlib import Path

ROLES = {"bootloader": (0, 0x8000), "partition_table": (0x8000, 0x1000),
         "ota_data_initial": (0xD000, 0x2000), "application": (0x10000, 0x1C0000)}
LOCAL_SOURCES = ["CMakeLists.txt", "main\\CMakeLists.txt", "main\\main.c",
                 "sdkconfig.defaults", "partitions_eh_cp_ota_4m.csv"]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def validate(manifest_path, expected_digest, root):
    manifest_path = Path(manifest_path).resolve()
    raw = manifest_path.read_bytes()
    if len(expected_digest) != 64 or sha(raw) != expected_digest.lower():
        raise ValueError("recovery manifest digest does not match the builder's pin")
    m = json.loads(raw)
    lock = json.loads((root / "tools/release/hosted-c6-release.json").read_text())
    if m.get("format") != "orcsdr-c6-rom-recovery-v1" or m.get("flash_size_bytes") != 0x400000:
        raise ValueError("unsupported recovery format or flash capacity")
    for key in ("target", "hosted_version", "source_repository", "source_revision", "source_project", "idf_version"):
        if m.get(key) != lock[key]:
            raise ValueError("recovery provenance mismatch: " + key)
    project = root / lock["source_project"]
    local = "\n".join(name + "=" + sha((project / name.replace("\\", "/")).read_bytes())
                      for name in LOCAL_SOURCES)
    if m.get("local_source_sha256") != sha(local.encode()):
        raise ValueError("recovery local source hash does not match this checkout")
    segments = m.get("segments", [])
    if len(segments) != len(ROLES) or {s.get("role") for s in segments} != set(ROLES):
        raise ValueError("recovery requires exactly bootloader, table, OTA data and application")
    result = []
    names = set()
    for s in segments:
        offset, capacity = ROLES[s["role"]]
        name = s["file"]
        if not isinstance(name, str) or Path(name).name != name or "\\" in name or not name.endswith(".bin"):
            raise ValueError("recovery file must be a local binary basename")
        symbol = "".join(c if c.isascii() and c.isalnum() else "_" for c in name)
        if symbol in names:
            raise ValueError("ambiguous embedded binary symbol")
        names.add(symbol)
        path = (manifest_path.parent / name).resolve()
        if path.parent != manifest_path.parent:
            raise ValueError("recovery file escapes package directory")
        data = path.read_bytes()
        # Full erase sectors must also remain inside the permitted partition.
        erase_size = (len(data) + 4095) // 4096 * 4096
        if int(s["offset"], 0) != offset or not data or len(data) % 4 or erase_size > capacity:
            raise ValueError("recovery segment exceeds its permitted flash range")
        if len(data) != s["size_bytes"] or sha(data) != s["sha256"]:
            raise ValueError("recovery binary size/hash mismatch")
        if s["role"] in ("application", "bootloader"):
            if len(data) < 24 or data[0] != 0xE9 or int.from_bytes(data[12:14], "little") != 13:
                raise ValueError("recovery image is not an ESP32-C6 image")
        if s["role"] == "ota_data_initial" and (len(data) != 8192 or any(b != 255 for b in data)):
            raise ValueError("unexpected initial OTA selection data")
        result.append(dict(s, path=path, symbol=symbol, hosted_version=m["hosted_version"]))
    return sorted(result, key=lambda s: int(s["offset"], 0))


def generate(segments, output):
    output.mkdir(parents=True, exist_ok=True)
    lines = ['#include "orcsdr_c6_rom_update.h"']
    for s in segments:
        lines.append(f'extern const uint8_t image_{s["symbol"]}[] asm("_binary_{s["symbol"]}_start");')
    lines.append("static const orc_c6_rom_segment_t segments[] = {")
    for s in segments:
        digest = ",".join("0x" + s["sha256"][i:i+2] for i in range(0, 64, 2))
        lines.append(f'{{{s["offset"]},image_{s["symbol"]},{s["size_bytes"]},{{{digest}}}}},')
    lines += ["};", "static const orc_c6_rom_manifest_t manifest = {true,13,0x400000,4,segments};",
              'extern "C" const orc_c6_rom_manifest_t* orcsdr_embedded_c6_rom_manifest() { return &manifest; }',
              'extern "C" const char* orcsdr_embedded_c6_rom_version() { return ' + json.dumps(segments[0]["hosted_version"]) + '; }']
    (output / "c6_rom_manifest.cpp").write_text("\n".join(lines) + "\n")
    # Bracket arguments keep paths literal; filenames were restricted above.
    paths = [str(s["path"]).replace("\\", "/") for s in segments]
    if any("]]" in p or ";" in p for p in paths):
        raise ValueError("unsupported package directory characters")
    (output / "c6_rom_files.cmake").write_text(
        "set(ORCSDR_C6_ROM_FILES\n" + "\n".join("[[" + p + "]]" for p in paths) + ")\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--expected-manifest-sha256", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    generate(validate(args.manifest, args.expected_manifest_sha256, repo), args.output)
