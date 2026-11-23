#!/usr/bin/env python3
"""Hardware-in-the-loop and self-check verifier for RTL_SUPPORT REPORT.

  python tools/test_support_report_device.py --self-check
  python tools/test_support_report_device.py --port COM17
  python tools/test_support_report_device.py --port COM17 --pairing-key path/to/key

Opens the Tab5 USB Serial/JTAG port without asserting DTR or RTS (so the board
is not reset), checks RTL_HELP, validates RTL_SUPPORT REPORT schema/privacy/
read-only idempotence, and when --pairing-key is supplied on a streaming
receiver, verifies RTL_STOP -> stopped report -> RTL_TUNE restored streaming.
"""
from __future__ import annotations

import argparse
import hashlib
import hmac
import pathlib
import re
import secrets
import sys
import time

HELP_LINE = "RTL_SUPPORT REPORT            - read-only, copy/paste support report (no auth)"
BEGIN_LINE = "ORC_SUPPORT_BEGIN schema=1 read_only=1 private_fields=excluded"
END_LINE = "ORC_SUPPORT_END"
LINE_PREFIXES = (
    BEGIN_LINE,
    "ORC_SUPPORT_BUILD ",
    "ORC_SUPPORT_USB ",
    "ORC_SUPPORT_RECEIVER ",
    "ORC_SUPPORT_RADIO ",
    "ORC_SUPPORT_STREAM ",
    "ORC_SUPPORT_SYSTEM ",
    END_LINE,
)
INT_FIELDS = (
    "receiver_ready",
    "installed",
    "metrics_ok",
    "frequency_hz",
    "target_sps",
    "effective_sps",
    "bytes",
    "short_transfers",
    "overruns",
    "consumer_drops",
    "uptime_ms",
    "reset_reason",
    "heap_free",
    "dma_free",
    "dma_largest",
    "psram_free",
)
STR_FIELDS = (
    "firmware",
    "build",
    "driver",
    "vid",
    "pid",
    "speed",
    "profile",
    "state",
    "last_error",
    "capture",
    "band",
)
FORBIDDEN_TOKENS = (
    "ssid",
    "password",
    "token=",
    "ip_addr",
    "mac=",
    "serial=",
    "latitude",
    "longitude",
    "pair_key",
)
CRASH_MARKERS = ("ESP-ROM:", "Guru Meditation", "assert failed", "Rebooting...")
KV_RE = re.compile(r'(\w+)=(?:"([^"]*)"|([^\s]+))')


def _read_line(port) -> str | None:
    raw = port.readline()
    if not raw:
        return None
    line = raw.decode("utf-8", "replace").rstrip("\r\n")
    if any(marker in line for marker in CRASH_MARKERS):
        raise RuntimeError(f"device crashed or reset: {line}")
    return line


def send_command(port, line: str) -> None:
    port.write((line + "\n").encode("ascii"))
    flush = getattr(port, "flush", None)
    if callable(flush):
        flush()


def collect_block(port, command: str, begin_prefix: str, end_exact: str, timeout: float) -> list[str]:
    send_command(port, command)
    lines: list[str] = []
    started = False
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = _read_line(port)
        if line is None:
            continue
        if not started and line.startswith(begin_prefix):
            started = True
        if started:
            lines.append(line)
            if line == end_exact:
                return lines
    raise TimeoutError(f"{command!r} did not complete within {timeout:.1f}s (started={started}, lines={len(lines)})")


def wait_for_prefixes(port, prefixes: tuple[str, ...], timeout: float) -> str:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = _read_line(port)
        if line is None:
            continue
        if any(line.startswith(prefix) for prefix in prefixes):
            return line
    raise TimeoutError(f"Timed out waiting for one of {prefixes}")


def validate_report(lines: list[str]) -> dict[str, object]:
    if len(lines) != 8:
        raise ValueError(f"expected 8 report lines, got {len(lines)}: {lines!r}")
    for index, (line, prefix) in enumerate(zip(lines, LINE_PREFIXES)):
        if len(line) >= 320:
            raise ValueError(f"report line {index} exceeds 319 bytes ({len(line)}): {line!r}")
        if "\n" in line or "\r" in line:
            raise ValueError(f"report line {index} contains embedded newline: {line!r}")
        if any(marker in line for marker in CRASH_MARKERS):
            raise RuntimeError(f"crash marker inside report: {line}")
        if index in (0, 7):
            if line != prefix:
                raise ValueError(f"report line {index} expected {prefix!r}, got {line!r}")
        elif not line.startswith(prefix):
            raise ValueError(f"report line {index} expected prefix {prefix!r}, got {line!r}")

    joined_lower = "\n".join(lines).lower()
    for token in FORBIDDEN_TOKENS:
        if token in joined_lower:
            raise ValueError(f"forbidden privacy token {token!r} found in report")

    parsed: dict[str, object] = {}
    for line in lines[1:7]:
        for match in KV_RE.finditer(line):
            key = match.group(1)
            val = match.group(2) if match.group(2) is not None else match.group(3)
            parsed[key] = val

    for key in STR_FIELDS:
        if key not in parsed:
            raise ValueError(f"missing string field {key!r} in report")
    for key in INT_FIELDS:
        if key not in parsed:
            raise ValueError(f"missing integer field {key!r} in report")
        try:
            parsed[key] = int(str(parsed[key]), 10)
        except ValueError as exc:
            raise ValueError(f"non-integer value for {key!r}: {parsed[key]!r}") from exc
    return parsed


def query_report(port, timeout: float) -> tuple[list[str], dict[str, object]]:
    lines = collect_block(port, "RTL_SUPPORT REPORT", "ORC_SUPPORT_BEGIN", END_LINE, timeout)
    return lines, validate_report(lines)


def authenticate(port, pairing_key_path: pathlib.Path, timeout: float) -> None:
    key_hex = pairing_key_path.read_text(encoding="ascii").strip()
    key = bytes.fromhex(key_hex)
    if len(key) != 32:
        raise ValueError("Pairing key must contain exactly 32 bytes")
    send_command(port, f"PAIR {key.hex()}")
    pair_reply = wait_for_prefixes(port, ("PAIR_OK", "PAIR_LOCKED", "PAIR_INVALID"), timeout)
    if pair_reply != "PAIR_OK":
        raise RuntimeError(f"Pairing refused: {pair_reply}")
    nonce = secrets.token_bytes(16)
    host_hmac = hmac.new(key, b"host" + nonce, hashlib.sha256).hexdigest()
    send_command(port, f"AUTH {nonce.hex()} {host_hmac}")
    auth_reply = wait_for_prefixes(port, ("AUTH_OK ", "AUTH_DENIED", "AUTH_ERROR", "AUTH_INVALID"), timeout)
    expected = hmac.new(key, b"device" + nonce, hashlib.sha256).hexdigest()
    if auth_reply != f"AUTH_OK {expected}":
        raise RuntimeError("Device authentication failed")


def run_device_check(
    port,
    *,
    timeout: float = 5.0,
    wait_ready: float = 0.0,
    pairing_key: pathlib.Path | None = None,
    dwell: float = 1.0,
) -> dict[str, object]:
    help_lines = collect_block(port, "RTL_HELP", "RTL_HELP_BEGIN", "RTL_HELP_END", timeout)
    if HELP_LINE not in help_lines:
        raise ValueError("RTL_HELP did not list RTL_SUPPORT REPORT")

    if wait_ready > 0:
        ready_deadline = time.monotonic() + wait_ready
        while True:
            lines1, parsed1 = query_report(port, timeout)
            if parsed1["receiver_ready"] == 1:
                break
            if time.monotonic() >= ready_deadline:
                raise TimeoutError("receiver_ready remained 0 past --wait-ready deadline")
            time.sleep(0.5)
    else:
        lines1, parsed1 = query_report(port, timeout)

    time.sleep(dwell)
    lines2, parsed2 = query_report(port, timeout)

    if int(parsed2["uptime_ms"]) < int(parsed1["uptime_ms"]):
        raise ValueError(f"uptime_ms regressed: {parsed1['uptime_ms']} -> {parsed2['uptime_ms']}")

    for field in (
        "receiver_ready",
        "installed",
        "profile",
        "state",
        "capture",
        "band",
        "frequency_hz",
        "target_sps",
    ):
        if parsed2[field] != parsed1[field]:
            raise ValueError(
                f"read-only query changed {field}: {parsed1[field]!r} -> {parsed2[field]!r}"
            )

    was_streaming = parsed1["capture"] == "running" and parsed1["state"] == "STREAMING"
    if was_streaming:
        if parsed1["receiver_ready"] != 1 or parsed1["installed"] != 1:
            raise ValueError("streaming report must show receiver_ready=1 and installed=1")
        if int(parsed1["effective_sps"]) <= 0 or int(parsed2["effective_sps"]) <= 0:
            raise ValueError(
                f"expected positive effective_sps across streaming reports, "
                f"got {parsed1['effective_sps']} -> {parsed2['effective_sps']}"
            )
        if int(parsed2["bytes"]) <= int(parsed1["bytes"]):
            raise ValueError(
                f"expected bytes counter to advance while streaming ({parsed1['bytes']} -> {parsed2['bytes']})"
            )

    stopped_report: tuple[list[str], dict[str, object]] | None = None
    restored_report: tuple[list[str], dict[str, object]] | None = None

    if pairing_key is not None and was_streaming:
        orig_band = str(parsed1["band"])
        orig_freq = int(parsed1["frequency_hz"])
        authenticate(port, pairing_key, timeout)
        body_exc = False
        try:
            send_command(port, "RTL_STOP")
            stop_reply = wait_for_prefixes(port, ("RTL_STOPPING", "RTL_STOP_RESULT"), timeout)
            if not (stop_reply == "RTL_STOPPING" or stop_reply.startswith("RTL_STOP_RESULT")):
                raise RuntimeError(f"unexpected RTL_STOP reply: {stop_reply}")
            stop_deadline = time.monotonic() + max(timeout, 6.0)
            while time.monotonic() < stop_deadline:
                s_lines, s_parsed = query_report(port, timeout)
                if s_parsed["capture"] != "running" and s_parsed["state"] != "STREAMING":
                    stopped_report = (s_lines, s_parsed)
                    break
                time.sleep(0.25)
            if stopped_report is None:
                raise TimeoutError("receiver did not leave STREAMING/running after RTL_STOP")
            if stopped_report[1]["receiver_ready"] != 1:
                raise ValueError("stopped report lost receiver_ready=1")
        except BaseException:
            body_exc = True
            raise
        finally:
            try:
                send_command(port, f"RTL_TUNE {orig_band} {orig_freq}")
                tune_reply = wait_for_prefixes(
                    port, ("RTL_TUNE_OK", "RTL_TUNE_INVALID", "RTL_TUNE_UNAVAILABLE"), timeout
                )
                if not tune_reply.startswith("RTL_TUNE_OK"):
                    raise RuntimeError(f"failed to restore receiver tuning ({orig_band} {orig_freq}): {tune_reply}")
                restore_deadline = time.monotonic() + max(timeout, 10.0)
                while time.monotonic() < restore_deadline:
                    r_lines, r_parsed = query_report(port, timeout)
                    if (
                        r_parsed["capture"] == "running"
                        and r_parsed["state"] == "STREAMING"
                        and int(r_parsed["effective_sps"]) > 0
                        and str(r_parsed["band"]) == orig_band
                        and int(r_parsed["frequency_hz"]) == orig_freq
                    ):
                        restored_report = (r_lines, r_parsed)
                        break
                    time.sleep(0.35)
                if restored_report is None:
                    raise TimeoutError(
                        "receiver did not return to STREAMING/running at original tuning after RTL_TUNE"
                    )
            except Exception as restore_exc:
                if body_exc:
                    print(f"WARN: receiver restoration also failed: {restore_exc}", file=sys.stderr)
                else:
                    raise

    return {
        "help_lines": len(help_lines),
        "initial": (lines1, parsed1),
        "second": (lines2, parsed2),
        "stopped": stopped_report,
        "restored": restored_report,
    }


def format_summary(result: dict[str, object]) -> str:
    parts = [f"RTL_HELP lines={result['help_lines']}"]
    for label in ("initial", "second", "stopped", "restored"):
        entry = result.get(label)
        if entry is None:
            continue
        lines, parsed = entry
        parts.append(
            f"\n### {label} (state={parsed['state']} capture={parsed['capture']} "
            f"effective_sps={parsed['effective_sps']} bytes={parsed['bytes']})\n```text\n"
            + "\n".join(lines)
            + "\n```"
        )
    return "\n".join(parts) + "\n"


def self_check() -> None:
    import tempfile

    class FakePort:
        def __init__(self, handler):
            self.handler = handler
            self.queue: list[bytes] = []
            self.sent: list[str] = []

        def write(self, data: bytes) -> None:
            cmd = data.decode("ascii").rstrip("\n")
            self.sent.append(cmd)
            self.queue.extend(self.handler(cmd))

        def readline(self) -> bytes:
            return self.queue.pop(0) if self.queue else b""

    def make_report(
        *,
        state: str = "STREAMING",
        capture: str = "running",
        band: str = "SHORTWAVE",
        frequency_hz: int = 7100000,
        effective_sps: int = 2399788,
        total_bytes: int = 1024,
        uptime_ms: int = 10000,
        serial_leak: bool = False,
        truncate: bool = False,
    ) -> list[bytes]:
        extra = ' serial="00000001"' if serial_leak else ""
        lines = [
            f"{BEGIN_LINE}\n",
            'ORC_SUPPORT_BUILD firmware="v0.6.0" build="test" driver="0.9.6"\n',
            f'ORC_SUPPORT_USB receiver_ready=1 vid=0bda pid=2838 speed="high"{extra}\n',
            f'ORC_SUPPORT_RECEIVER installed=1 profile="blog_v4l_r828s" state="{state}" last_error="ESP_OK"\n',
            f'ORC_SUPPORT_RADIO capture="{capture}" band="{band}" frequency_hz={frequency_hz}\n',
            f"ORC_SUPPORT_STREAM metrics_ok=1 target_sps=2400000 effective_sps={effective_sps} "
            f"bytes={total_bytes} short_transfers=0 overruns=0 consumer_drops=0\n",
            f"ORC_SUPPORT_SYSTEM uptime_ms={uptime_ms} reset_reason=11 heap_free=21468564 "
            "dma_free=18555 dma_largest=18432 psram_free=21449352\n",
            f"{END_LINE}\n",
        ]
        if truncate:
            lines.pop(4)
        return [line.encode("utf-8") for line in lines]

    key = bytes(range(32))
    with tempfile.TemporaryDirectory(prefix="orc-support-hil-") as tmpdir:
        key_file = pathlib.Path(tmpdir) / "test.key"
        key_file.write_text(key.hex(), encoding="ascii")

        state = {"mode": "streaming", "reports": 0}

        def full_handler(cmd: str) -> list[bytes]:
            if cmd == "RTL_HELP":
                return [b"RTL_HELP_BEGIN\n", f"{HELP_LINE}\n".encode("ascii"), b"RTL_HELP_END\n"]
            if cmd.startswith("PAIR "):
                return [b"PAIR_OK\n"]
            if cmd.startswith("AUTH "):
                nonce = bytes.fromhex(cmd.split()[1])
                dev = hmac.new(key, b"device" + nonce, hashlib.sha256).hexdigest()
                return [f"AUTH_OK {dev}\n".encode("ascii")]
            if cmd == "RTL_STOP":
                state["mode"] = "stopped"
                return [b"RTL_STOPPING\n"]
            if cmd == "RTL_TUNE SHORTWAVE 7100000":
                state["mode"] = "streaming"
                return [b"RTL_TUNE_OK band=SHORTWAVE frequency_hz=7100000\n"]
            if cmd == "RTL_SUPPORT REPORT":
                state["reports"] += 1
                if state["mode"] == "stopped":
                    return make_report(state="READY", capture="ready", effective_sps=0, total_bytes=2048,
                                       uptime_ms=10000 + state["reports"] * 100)
                return make_report(total_bytes=1024 * state["reports"], uptime_ms=10000 + state["reports"] * 100)
            raise AssertionError(f"unexpected command: {cmd}")

        res = run_device_check(FakePort(full_handler), timeout=0.5, pairing_key=key_file, dwell=0.0)
        assert res["stopped"] is not None and res["stopped"][1]["capture"] == "ready"
        assert res["restored"] is not None and res["restored"][1]["state"] == "STREAMING"

        # Frequency or state drift between two unauthenticated queries must fail.
        drift_calls = {"n": 0}

        def drift_handler(cmd: str) -> list[bytes]:
            if cmd == "RTL_HELP":
                return [b"RTL_HELP_BEGIN\n", f"{HELP_LINE}\n".encode("ascii"), b"RTL_HELP_END\n"]
            if cmd == "RTL_SUPPORT REPORT":
                drift_calls["n"] += 1
                freq = 7100000 if drift_calls["n"] == 1 else 7200000
                return make_report(frequency_hz=freq, total_bytes=1024 * drift_calls["n"], uptime_ms=10000 + drift_calls["n"])
            raise AssertionError(f"unexpected command: {cmd}")

        try:
            run_device_check(FakePort(drift_handler), timeout=0.3, dwell=0.0)
        except ValueError:
            pass
        else:
            raise AssertionError("frequency drift across repeated reports must fail")

        # Lost RTL_STOP reply must still invoke RTL_TUNE restoration in finally.
        lost_stop_sent: list[str] = []

        def lost_stop_handler(cmd: str) -> list[bytes]:
            lost_stop_sent.append(cmd)
            if cmd == "RTL_HELP":
                return [b"RTL_HELP_BEGIN\n", f"{HELP_LINE}\n".encode("ascii"), b"RTL_HELP_END\n"]
            if cmd.startswith("PAIR "):
                return [b"PAIR_OK\n"]
            if cmd.startswith("AUTH "):
                nonce = bytes.fromhex(cmd.split()[1])
                dev = hmac.new(key, b"device" + nonce, hashlib.sha256).hexdigest()
                return [f"AUTH_OK {dev}\n".encode("ascii")]
            if cmd == "RTL_STOP":
                return []  # simulate lost reply -> TimeoutError before stop loop
            if cmd == "RTL_TUNE SHORTWAVE 7100000":
                return [b"RTL_TUNE_OK band=SHORTWAVE frequency_hz=7100000\n"]
            if cmd == "RTL_SUPPORT REPORT":
                return make_report(total_bytes=1024 * len(lost_stop_sent), uptime_ms=10000 + len(lost_stop_sent))
            raise AssertionError(f"unexpected command: {cmd}")

        try:
            run_device_check(FakePort(lost_stop_handler), timeout=0.05, pairing_key=key_file, dwell=0.0)
        except TimeoutError:
            pass
        else:
            raise AssertionError("missing RTL_STOP reply must raise TimeoutError")
        assert "RTL_TUNE SHORTWAVE 7100000" in lost_stop_sent, "RTL_TUNE restore must run even when RTL_STOP ack is lost"

        # Restoration to wrong frequency must fail rather than pass.
        wrong_restore_mode = {"stopped": False, "reports": 0}

        def wrong_restore_handler(cmd: str) -> list[bytes]:
            if cmd == "RTL_HELP":
                return [b"RTL_HELP_BEGIN\n", f"{HELP_LINE}\n".encode("ascii"), b"RTL_HELP_END\n"]
            if cmd.startswith("PAIR "):
                return [b"PAIR_OK\n"]
            if cmd.startswith("AUTH "):
                nonce = bytes.fromhex(cmd.split()[1])
                dev = hmac.new(key, b"device" + nonce, hashlib.sha256).hexdigest()
                return [f"AUTH_OK {dev}\n".encode("ascii")]
            if cmd == "RTL_STOP":
                wrong_restore_mode["stopped"] = True
                return [b"RTL_STOPPING\n"]
            if cmd == "RTL_TUNE SHORTWAVE 7100000":
                wrong_restore_mode["stopped"] = False
                return [b"RTL_TUNE_OK band=SHORTWAVE frequency_hz=7100000\n"]
            if cmd == "RTL_SUPPORT REPORT":
                wrong_restore_mode["reports"] += 1
                if wrong_restore_mode["stopped"]:
                    return make_report(state="READY", capture="ready", effective_sps=0, total_bytes=4096,
                                       uptime_ms=10000 + wrong_restore_mode["reports"])
                freq = 7100000 if wrong_restore_mode["reports"] <= 2 else 99100000
                return make_report(frequency_hz=freq, total_bytes=1024 * wrong_restore_mode["reports"],
                                   uptime_ms=10000 + wrong_restore_mode["reports"])
            raise AssertionError(f"unexpected command: {cmd}")

        try:
            run_device_check(FakePort(wrong_restore_handler), timeout=0.05, pairing_key=key_file, dwell=0.0)
        except TimeoutError:
            pass
        else:
            raise AssertionError("restored report on wrong frequency must fail")

    # Missing RTL_SUPPORT REPORT in RTL_HELP must fail.
    try:
        run_device_check(FakePort(lambda c: [b"RTL_HELP_BEGIN\n", b"RTL_HELP_END\n"]), timeout=0.2, dwell=0.0)
    except ValueError:
        pass
    else:
        raise AssertionError("missing RTL_SUPPORT REPORT in RTL_HELP must fail")

    # Privacy token in report must fail.
    try:
        validate_report([line.decode("utf-8").rstrip("\n") for line in make_report(serial_leak=True)])
    except ValueError:
        pass
    else:
        raise AssertionError("privacy violation must fail")

    # Wrong line count must fail.
    try:
        validate_report([line.decode("utf-8").rstrip("\n") for line in make_report(truncate=True)])
    except ValueError:
        pass
    else:
        raise AssertionError("truncated report must fail")

    # Crash marker must raise RuntimeError.
    try:
        run_device_check(FakePort(lambda c: [b"Guru Meditation Error: Core 0\n"]), timeout=0.2, dwell=0.0)
    except RuntimeError:
        pass
    else:
        raise AssertionError("crash marker must raise RuntimeError")

    print("PASS self-check: streaming/stop/restore cycle, help gate, privacy check, line bounds, crash handling")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--self-check", action="store_true", help="run offline protocol/parser tests")
    parser.add_argument("--port", help="Tab5 serial port, e.g. COM17; never auto-detected")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--wait-ready", type=float, default=0.0, help="seconds to wait for receiver_ready=1")
    parser.add_argument("--pairing-key", type=pathlib.Path, help="optional 32-byte hex key to test RTL_STOP/restore")
    parser.add_argument("--summary-out", type=pathlib.Path, help="optional path to write a short markdown summary")
    args = parser.parse_args()

    if args.self_check:
        self_check()
        return 0
    if not args.port:
        parser.error("give --port (the tool never picks a port itself) or --self-check")

    import serial  # pyserial; imported only for hardware runs

    port = serial.Serial()
    port.port, port.baudrate, port.timeout = args.port, args.baud, 0.2
    port.dtr = False
    port.rts = False
    port.open()
    try:
        port.reset_input_buffer()
        result = run_device_check(
            port,
            timeout=args.timeout,
            wait_ready=args.wait_ready,
            pairing_key=args.pairing_key,
        )
    finally:
        port.close()

    summary = format_summary(result)
    print(summary, end="")
    if args.summary_out is not None:
        args.summary_out.write_text(summary, encoding="utf-8")
    _, initial = result["initial"]
    print(
        f"PASS: RTL_SUPPORT REPORT verified on {args.port} "
        f"(state={initial['state']} capture={initial['capture']} "
        f"stopped_checked={'yes' if result['stopped'] is not None else 'no'})"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
