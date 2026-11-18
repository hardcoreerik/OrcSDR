#!/usr/bin/env python3
"""Portable, fail-closed ESP-IDF build/flash helper for LilyGO T-Display P4 AMOLED.

Run from ANY working directory after activating ESP-IDF 5.5.4.
Requires ESP-IDF's Python environment (pyserial + esptool).
There is no board-identity attestation: operator must verify the exact LilyGO SKU.
"""
from __future__ import annotations

import argparse
from datetime import datetime
import os
from pathlib import Path
import re
import subprocess
import sys

PROJECT = Path(__file__).resolve().parents[1]
SHARED_DEFAULTS = PROJECT.parent / "orcsdr-p4-hosyond-console" / "sdkconfig.defaults"
BOARD_DEFAULTS = PROJECT / "sdkconfig.defaults"
V3_DEFAULTS = PROJECT / "sdkconfig.silicon-v3"
# ESP32-P4 native USB Serial/JTAG, WCH, Silicon Labs and FTDI USB/UART bridges.
# A single plausible port still must pass esptool's ESP32-P4 identification.
USB_VENDOR_IDS = {0x303A, 0x1A86, 0x10C4, 0x0403}


class PreflightError(RuntimeError):
    pass


def idf_available() -> str:
    command = Path(os.environ.get("IDF_PATH", "")) / "tools" / "idf.py"
    if not os.environ.get("IDF_PATH") or not command.is_file():
        raise PreflightError(
            "Activate the ESP-IDF 5.5.4 environment first (idf.py and IDF_PATH required)."
        )
    return str(command)


def select_port(requested: str | None) -> str:
    if requested:
        return requested
    try:
        from serial.tools import list_ports
    except ImportError as exc:
        raise PreflightError("pyserial is unavailable; activate ESP-IDF or use --port.") from exc
    ports = list(list_ports.comports())
    candidates = [p.device for p in ports if p.vid in USB_VENDOR_IDS]
    if len(candidates) != 1:
        shown = ", ".join(
            f"{p.device} ({p.description}, VID={p.vid}, PID={p.pid})"
            for p in ports
        ) or "none"
        raise PreflightError(
            f"Automatic port selection is ambiguous ({len(candidates)} candidates). "
            f"Connected ports: {shown}. Supply --port explicitly."
        )
    return candidates[0]


def probe_chip(port: str) -> tuple[int, str]:
    args = [sys.executable, "-m", "esptool", "--chip", "esp32p4",
            "--port", port, "flash_id"]
    print("Probing the connected ESP32-P4 on", port, flush=True)
    try:
        result = subprocess.run(args, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=45, check=False)
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise PreflightError(f"Unable to identify board on {port}: {exc}") from exc
    output = result.stdout or ""
    if result.returncode or not re.search(r"ESP32[- ]P4", output, re.I):
        raise PreflightError(f"Not a verified ESP32-P4 on {port}:\n{output}")
    match = re.search(r"\brevision\s*:?\s*v?(\d+)\.(\d+)", output, re.I)
    if not match:
        raise PreflightError(
            "ESP32-P4 detected but its silicon revision was not reported. "
            "Refusing to select a flash image automatically.\n" + output
        )
    print(f"Detected ESP32-P4 revision v{match.group(1)}.{match.group(2)}")
    return int(match.group(1)), output


def build_dir(revision: int) -> Path:
    return PROJECT / ("build-lilygo-rev3" if revision >= 3 else "build-lilygo-rev1")


def build(revision: int) -> Path:
    idf = idf_available()
    if revision not in (1, 3):
        raise PreflightError("Only silicon major revisions 1 and 3 are supported.")
    overlays = [SHARED_DEFAULTS, BOARD_DEFAULTS]
    if revision == 3:
        overlays.append(V3_DEFAULTS)
    for path in overlays:
        if not path.is_file():
            raise PreflightError(f"Required build configuration is missing: {path}")
    directory = build_dir(revision)
    args = [
        sys.executable, idf, "-B", str(directory), "-D", "IDF_TARGET=esp32p4",
        "-D", "SDKCONFIG=" + str(directory / "sdkconfig"),
        "-D", "SDKCONFIG_DEFAULTS=" + ";".join(str(p) for p in overlays),
        "build"
    ]
    print("Building LilyGO profile; silicon revision:", revision, flush=True)
    subprocess.run(args, cwd=PROJECT, check=True)
    config_file = directory / "sdkconfig"
    try:
        config = config_file.read_text(encoding="utf-8")
    except OSError as exc:
        raise PreflightError(f"ESP-IDF did not produce {config_file}") from exc
    required = ["CONFIG_ORCSDR_BOARD_LILYGO_T_DISPLAY_P4_AMOLED=y",
                "CONFIG_IDF_TARGET=\"esp32p4\""]
    required.append("CONFIG_ESP32P4_REV_MIN_300=y" if revision == 3
                    else "CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y")
    for setting in required:
        if setting not in config:
            raise PreflightError(
                f"Built sdkconfig missing {setting}; refusing to approve this image."
            )
    print("Build configuration verified:", config_file)
    return directory


def backup(port: str, identity: str) -> Path:
    match = re.search(r"Detected flash size:\s*(\d+)\s*(MB|KB)\b", identity, re.I)
    if not match:
        raise PreflightError(
            "Could not determine flash capacity from esptool. No backup or flash "
            "performed. Create a verified full-flash backup manually, then "
            "use --no-backup only if you accept the recovery risk."
        )
    size = int(match.group(1)) * (1024 * 1024 if match.group(2).upper() == "MB" else 1024)
    if size <= 0 or size > 64 * 1024 * 1024:
        raise PreflightError(f"Unexpected flash capacity {size}; refusing backup.")
    folder = PROJECT / "build-lilygo-backups"
    folder.mkdir(parents=True, exist_ok=True)
    output = folder / f"original-{datetime.now().strftime('%Y%m%d-%H%M%S')}.bin"
    print(f"Saving FULL original {size}-byte flash image to {output}", flush=True)
    subprocess.run([sys.executable, "-m", "esptool", "--chip", "esp32p4",
                    "--port", port, "read_flash", "0", str(size), str(output)],
                   check=True)
    if not output.is_file() or output.stat().st_size != size:
        raise PreflightError("Backup missing/incomplete. Refusing to flash.")
    print("Backup verified:", output)
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    build_command = commands.add_parser("build", help="Build without a connected board")
    build_command.add_argument("--revision", choices=("1", "3"), required=True,
                               help="ESP32-P4 silicon MAJOR revision")
    for name in ("probe", "backup", "flash", "monitor"):
        sub = commands.add_parser(name)
        sub.add_argument("--port", help="Serial port; otherwise detect one candidate")
        if name == "flash":
            sub.add_argument("--experimental", action="store_true",
                             help="Explicitly acknowledge incomplete LilyGO hardware support")
            sub.add_argument("--no-backup", action="store_true",
                             help="Skip full-flash backup ONLY if one already exists")
            sub.add_argument("--monitor", action="store_true",
                             help="Open serial monitor after flashing")
    args = parser.parse_args()
    try:
        if args.command == "build":
            build(int(args.revision))
            return 0
        port = select_port(args.port)
        revision, identity = probe_chip(port)
        if revision not in (1, 3):
            raise PreflightError(f"Unrecognized ESP32-P4 major revision {revision}.")
        if args.command == "probe":
            print("Port:", port, "(chip revision only; board SKU NOT confirmed)")
            return 0
        if args.command == "backup":
            backup(port, identity)
            return 0
        if args.command == "flash" and not args.experimental:
            raise PreflightError(
                "The LilyGO display/USB/power port is not validated. "
                "To proceed knowingly, specify --experimental AFTER confirming "
                "AMOLED + SX1262 hardware and a recovery method."
            )
        if args.command == "flash":
            directory = build(revision)
            if not args.no_backup:
                backup(port, identity)
        else:
            directory = build_dir(revision)
            if not directory.is_dir():
                raise PreflightError(f"No build at {directory}; run build first.")
        idf = idf_available()
        if args.command == "flash":
            print("EXPERIMENTAL FLASH: LilyGO AMOLED/SX1262 ONLY; not a finished port.")
            subprocess.run([sys.executable, idf, "-B", str(directory), "-p", port, "flash"],
                           cwd=PROJECT, check=True)
        if args.command == "monitor" or (args.command == "flash" and args.monitor):
            subprocess.run([sys.executable, idf, "-B", str(directory), "-p", port, "monitor"],
                           cwd=PROJECT, check=True)
        return 0
    except (PreflightError, subprocess.CalledProcessError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
