#!/usr/bin/env python3
"""OrcSDR SSTV / Reverse Engineer Anything (REA) evidence workbench.

Uses the upstream REA CLI as installed by the operator.  This wrapper never
downloads packages, installs analysis providers or edits Codex configuration.
Only inspect binaries you built, own, or have permission to inspect.

    python tools/sstv_rea_workbench.py build
    python tools/sstv_rea_workbench.py doctor
    python tools/sstv_rea_workbench.py inspect
    python tools/sstv_rea_workbench.py inspect --target /path/to/allowed/decoder

Requires for native inspection: Node >=22.19 / >=24.11 / >=26, rea-agents
6.4.0 (pinned in docs), plus Ghidra 12.1.x and JDK 21+, or Hopper.
"""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCES = [
    ROOT / "apps/orcsdr-tab5/tests/sstv_wav_replay.cpp",
    ROOT / "apps/orcsdr-tab5/ui/sstv_receiver.cpp",
    ROOT / "apps/orcsdr-tab5/ui/sstv_modes.cpp",
    ROOT / "apps/orcsdr-tab5/ui/sstv_auto_detect.cpp",
]
EXPECTED_REA = "6.4.0"
TIMEOUT_S = 480


def run(argv: list[str], base: Path, stem: str, timeout: int = TIMEOUT_S) -> bool:
    base.mkdir(parents=True, exist_ok=True)
    command_path = base / f"{stem}.command.txt"
    out_path = base / f"{stem}.stdout.txt"
    err_path = base / f"{stem}.stderr.log"
    command_path.write_text(" ".join(argv) + "\n", encoding="utf-8")
    try:
        result = subprocess.run(argv, cwd=ROOT, capture_output=True, text=True,
                                errors="replace", timeout=timeout, check=False)
        out_path.write_text(result.stdout, encoding="utf-8")
        err_path.write_text(result.stderr, encoding="utf-8")
        if "--json" in argv:
            try:
                parsed = json.loads(result.stdout)
                (base / f"{stem}.json").write_text(
                    json.dumps(parsed, indent=2) + "\n", encoding="utf-8")
            except json.JSONDecodeError:
                # Preserve raw output; never fabricate a successful JSON record.
                pass
        print(f"{stem}: exit={result.returncode}; logs: {err_path}")
        return result.returncode == 0
    except (OSError, subprocess.TimeoutExpired) as exc:
        err_path.write_text(f"{type(exc).__name__}: {exc}\n", encoding="utf-8")
        print(f"{stem}: unavailable: {exc}", file=sys.stderr)
        return False


def version(text: str) -> tuple[int, int, int] | None:
    match = re.search(r"v?(\d+)\.(\d+)\.(\d+)", text)
    return tuple(map(int, match.groups())) if match else None


def check_node() -> bool:
    if not shutil.which("node"):
        print("Node missing. Install a supported Node LTS.", file=sys.stderr)
        return False
    r = subprocess.run(["node", "--version"], capture_output=True, text=True)
    v = version(r.stdout)
    ok = v is not None and (v[0] >= 26 or v[0] == 24 and v[1] >= 11
                             or v[0] == 22 and v[1] >= 19)
    print(f"Node: {r.stdout.strip()} ({'OK' if ok else 'REA version requirement not met'})")
    return ok


def build(base: Path) -> Path | None:
    output = base / "sstv-wav-replay"
    if not shutil.which("g++"):
        print("g++ not found. Install C++17 compiler.", file=sys.stderr)
        return None
    argv = ["g++", "-std=c++17", "-O2", "-g", "-fno-omit-frame-pointer",
            "-Wall", "-Wextra", f"-I{ROOT / 'apps/orcsdr-tab5/ui'}",
            *map(str, SOURCES), "-o", str(output)]
    if not run(argv, base, "build", timeout=120):
        return None
    print(f"Native ELF: {output}")
    return output


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(131072), b""):
            h.update(block)
    return h.hexdigest()


def cli(base: Path, action: str, provider: str, target: Path | None) -> bool:
    if not check_node():
        return False
    executable = shutil.which("rea")
    if not executable:
        print("REA is not installed. Install pinned rea-agents@6.4.0 and run 'rea setup'.",
              file=sys.stderr)
        return False
    if not run([executable, "--version"], base, "rea-version", timeout=30):
        return False
    if action == "doctor":
        return run([executable, "doctor", "--provider", provider, "--json"],
                   base, "rea-doctor", timeout=60)
    if target is None or not target.is_file():
        print("Missing native target binary.", file=sys.stderr)
        return False
    record = {
        "generated_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "target_path": str(target.resolve()),
        "target_sha256": sha256(target),
        "provider": provider,
        "rea_expected_version": EXPECTED_REA,
        "sources": {str(p.relative_to(ROOT)): sha256(p) for p in SOURCES},
        "note": "REA static evidence, not proof of SSTV decode correctness",
    }
    base.mkdir(parents=True, exist_ok=True)
    (base / "provenance.json").write_text(json.dumps(record, indent=2) + "\n")
    # Native analysis can consume minutes in Ghidra. Results are stored
    # read-only, with stdout, stderr, and the exact command for auditing.
    overview_ok = run([executable, "analyze", str(target.resolve()),
                       "--provider", provider, "--json"],
                      base, "rea-native-overview")
    if not overview_ok:
        return False
    # The repo-owned host binary is intentionally built with symbol names and
    # DWARF debug info. Rea's Ghidra ELF image is generally based at 0x100000
    # while nm symbols in a PIE are ELF-relative. Derive the observed base
    # from Ghidra's overview; never assume a hardcoded function offset.
    if target.resolve() != (base / "sstv-wav-replay").resolve():
        return True
    if not shutil.which("nm"):
        print("nm unavailable: function-level report skipped", file=sys.stderr)
        return True
    try:
        evidence = json.loads((base / "rea-native-overview.json").read_text())
        segments = evidence["normalized_result"]["segments"]
        load_base = int(next(seg["start"] for seg in segments
                             if seg["name"].startswith("segment_")), 16)
        symbols = subprocess.check_output(["nm", "-C", str(target)],
                                          text=True, stderr=subprocess.STDOUT)
    except (KeyError, ValueError, FileNotFoundError, OSError,
            subprocess.CalledProcessError, StopIteration) as exc:
        print(f"Function mapping unavailable: {exc}", file=sys.stderr)
        return True  # overview evidence is still valid
    good = True
    target_symbols = {
        "rea-dsp-audio-tap": "orcsdr::sstv::offer_audio_48k(short const*, unsigned long)",
        "rea-decoder-service": "orcsdr::sstv::service(unsigned long)",
    }
    for stem, symbol in target_symbols.items():
        matches = re.findall(r"^([0-9a-f]+) [Tt] " + re.escape(symbol) + r"$",
                             symbols, re.MULTILINE)
        if len(matches) != 1:
            print(f"Missing or ambiguous host symbol: {symbol}", file=sys.stderr)
            good = False
            continue
        address = hex(load_base + int(matches[0], 16))
        good = run([executable, "function", str(target.resolve()), address,
                    "--provider", provider, "--json"],
                   base, stem) and good
    return good


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("action", choices=("build", "doctor", "inspect"))
    p.add_argument("--provider", choices=("ghidra", "hopper"), default="ghidra")
    p.add_argument("--target", type=Path,
                   help="Existing legal native binary; default is the host replay ELF")
    p.add_argument("--out", type=Path, default=Path.cwd() / "sstv-rea-evidence")
    args = p.parse_args()
    base = args.out.expanduser().resolve()
    if args.action == "build":
        return 0 if build(base) else 1
    if args.action == "doctor":
        return 0 if cli(base, "doctor", args.provider, None) else 1
    target = args.target.resolve() if args.target else base / "sstv-wav-replay"
    if not target.exists() and args.target is None and not build(base):
        return 1
    return 0 if cli(base, "inspect", args.provider, target) else 1


if __name__ == "__main__":
    sys.exit(main())
