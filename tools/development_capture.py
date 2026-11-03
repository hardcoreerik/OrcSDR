#!/usr/bin/env python3
"""Private, bounded OrcSDR serial capture; never resets or authenticates."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time
from datetime import datetime, timezone


def snapshot_build(build, destination):
    build = Path(build).resolve()
    description = json.loads((build / "project_description.json").read_text())
    elf = (build / description["app_elf"]).resolve()
    if not elf.is_relative_to(build) or not elf.is_file():
        raise ValueError("ELF must exist inside the selected build directory")
    # Preserve symbols before another build can replace them.
    shutil.copy2(elf, destination / elf.name)
    shutil.copy2(description["config_file"], destination / "sdkconfig")
    for source in (build / (elf.stem + ".bin"), build / (elf.stem + ".map"),
                   build / "partition_table" / "partition-table.bin"):
        if source.is_file():
            shutil.copy2(source, destination / source.name)
    with (destination / elf.name).open("rb") as saved:
        digest = hashlib.file_digest(saved, "sha256").hexdigest()
    return {"build_dir": str(build), "elf": elf.name, "elf_sha256": digest}


def capture(stream, log, seconds, max_bytes, clock=time.monotonic):
    start = clock()
    next_poll = start
    total = 0
    stream.write(b"\n")  # Delimit any partial console line without discarding received evidence.
    while clock() - start < seconds:
        if clock() >= next_poll:
            stream.write(b"RTL_HEALTH BUILD\nRTL_HEALTH\n")
            next_poll = clock() + 30
        data = stream.read(4096)
        if not data:
            continue
        remaining = max_bytes - total
        log.write(data[:remaining])
        log.flush()
        total += min(len(data), remaining)
        if total >= max_bytes:
            return {"bytes": total, "stop": "size_limit", "complete": False}
    return {"bytes": total, "stop": "duration", "complete": True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=921600)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--seconds", type=float, default=300)
    parser.add_argument("--max-mib", type=int, default=32)
    args = parser.parse_args()
    if args.seconds <= 0 or args.max_mib <= 0 or args.baud <= 0:
        parser.error("seconds, max-mib and baud must be positive")
    repo = Path(__file__).resolve().parents[1]
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    destination = repo / ".local" / "diagnostics" / stamp
    destination.mkdir(parents=True)
    metadata = snapshot_build(args.build_dir, destination)
    metadata.update(port=args.port, baud=args.baud, started_utc=stamp,
                    note="Private raw evidence. Match device RTL_HEALTH_BUILD to elf_sha256 before decoding.")
    for field, command in (("git_head", ["git", "rev-parse", "HEAD"]),
                           ("git_status", ["git", "status", "--short"])):
        result = subprocess.run(command, cwd=repo, capture_output=True, text=True, check=False)
        metadata[field] = result.stdout.strip()
    manifest = destination / "manifest.json"
    manifest.write_text(json.dumps(metadata, indent=2))
    import serial
    stream = serial.Serial()
    stream.port, stream.baudrate, stream.timeout = args.port, args.baud, 0.2
    stream.write_timeout = 2
    stream.dtr = stream.rts = False
    try:
        stream.open()  # No reset, buffer clearing, authentication or receiver changes.
        with (destination / "serial.log").open("xb") as log:
            metadata["capture"] = capture(stream, log, args.seconds, args.max_mib * 1024 * 1024)
    except KeyboardInterrupt:
        metadata["capture"] = {"stop": "user", "complete": False}
    except serial.SerialException:
        metadata["capture"] = {"stop": "serial_error", "complete": False}
    finally:
        if stream.is_open:
            stream.close()
        manifest.write_text(json.dumps(metadata, indent=2))
    print(f"Private capture: {destination}")
    print(f"Stopped: {metadata['capture']['stop']}; raw logs are not safe to publish.")
    return 0 if metadata["capture"]["complete"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
