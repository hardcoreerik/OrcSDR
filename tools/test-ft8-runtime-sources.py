#!/usr/bin/env python3
"""Check the shared FT8 runtime CMake source manifest without building firmware."""

from pathlib import Path
import re
import sys
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "apps/orcsdr-tab5/ui/ft8_runtime_sources.cmake"


def main() -> int:
    source = MANIFEST.read_text(encoding="utf-8")
    match = re.search(
        r"set\(_ORCSDR_FT8_RUNTIME_SOURCE_NAMES\s*(.*?)\)", source, re.DOTALL
    )
    if match is None:
        print("FT8 runtime manifest source list was not found", file=sys.stderr)
        return 1

    names = re.findall(r"(?<![\w.-])([a-z0-9_]+\.cpp)(?![\w.-])", match.group(1))
    if not names:
        print("FT8 runtime manifest is empty", file=sys.stderr)
        return 1
    if len(names) != len(set(names)):
        print("FT8 runtime manifest contains duplicate source names", file=sys.stderr)
        return 1

    required = {
        "ft8_runtime.cpp",
        "ft8_native_backend.cpp",
        "ft8_audio_tap.cpp",
        "ft8_spectral_fft.cpp",
        "js8_native_backend.cpp",
        "js8_spectral.cpp",
        "wwv_tick.cpp",
    }
    missing = sorted(required - set(names))
    if missing:
        print(f"FT8 runtime manifest missing required sources: {', '.join(missing)}", file=sys.stderr)
        return 1

    absent = sorted(name for name in names if not (MANIFEST.parent / name).is_file())
    if absent:
        print(f"FT8 runtime manifest paths do not exist: {', '.join(absent)}", file=sys.stderr)
        return 1

    # Evaluate the manifest with CMake instead of trusting its spelling.
    with tempfile.TemporaryDirectory() as temp:
        script = Path(temp) / "check.cmake"
        output = Path(temp) / "sources.txt"
        manifest_path = MANIFEST.as_posix()
        script.write_text(
            f'include("{manifest_path}")\n'
            f'include("{manifest_path}")\n'
            f'file(WRITE "{output.as_posix()}" "${{ORCSDR_FT8_RUNTIME_SOURCES}}")\n',
            encoding="utf-8",
        )
        subprocess.run(["cmake", "-P", str(script)], check=True)
        resolved = output.read_text(encoding="utf-8").split(";")
        expected = [(MANIFEST.parent / name).resolve() for name in names]
        if [Path(path).resolve() for path in resolved] != expected:
            print("CMake source resolution or repeated include changed the list", file=sys.stderr)
            return 1

    print(f"ft8-runtime-sources: PASS ({len(names)} unique existing source paths)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
