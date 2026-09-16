#!/usr/bin/env python3
"""Repeatable Meshtastic TX/reference-RX/OrcSDR baseline laboratory."""

from __future__ import annotations

import argparse
import csv
import importlib.metadata
import json
import math
import random
import re
import sys
import threading
import time
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path

from serial import Serial
from serial.tools import list_ports


TOKEN_PREFIX = "ORC-LORA-TEST-"
KEY_VALUE_RE = re.compile(r"([a-z_]+)=([^\s]+)")


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def serial_inventory() -> list[dict]:
    return [
        {
            "port": item.device,
            "description": item.description,
            "manufacturer": item.manufacturer,
            "vid": f"{item.vid:04X}" if item.vid is not None else None,
            "pid": f"{item.pid:04X}" if item.pid is not None else None,
            "serial_number": item.serial_number,
            "location": item.location,
        }
        for item in sorted(list_ports.comports(), key=lambda value: value.device)
    ]


def parse_orcsdr_line(line: str) -> dict | None:
    prefixes = {
        "RTL_IQ_START": "capture_start",
        "RTL_IQ_DONE": "capture_done",
        "RTL_LORA_NATIVE_DONE": "decode_done",
        "RTL_LORA_CAPTURE_DROP": "capture_drop",
        "RTL_IQ_STATUS": "iq_status",
        "RTL_LORA_NATIVE_STATUS": "decoder_status",
        "RTL_LORA_PLAN_STATUS": "plan_status",
        "RTL_FREQ_STATUS": "frequency_status",
    }
    prefix = next((value for value in prefixes if line.startswith(value)), None)
    if prefix is None:
        return None
    values: dict[str, object] = {}
    for key, raw in KEY_VALUE_RE.findall(line):
        raw = raw.strip('"')
        if raw in ("true", "false"):
            values[key] = raw == "true"
            continue
        try:
            values[key] = float(raw) if "." in raw else int(raw)
        except ValueError:
            values[key] = raw
    return {"event_type": prefixes[prefix], "line_type": prefix, **values}


@dataclass
class EventLog:
    path: Path
    records: list[dict] = field(default_factory=list)
    lock: threading.Lock = field(default_factory=threading.Lock)

    def add(self, kind: str, **values) -> dict:
        record = {
            "timestamp": utc_now(),
            "monotonic": time.monotonic(),
            "kind": kind,
            **values,
        }
        with self.lock:
            self.records.append(record)
            with self.path.open("a", encoding="utf-8") as stream:
                stream.write(json.dumps(record, separators=(",", ":"), default=str) + "\n")
        return record

    def snapshot(self) -> list[dict]:
        with self.lock:
            return list(self.records)


class OrcConsole:
    def __init__(self, port: str, raw_path: Path, events: EventLog):
        self.port = port
        self.raw_path = raw_path
        self.events = events
        self.serial = Serial(port=None, baudrate=115200, timeout=0.2)
        self.serial.port = port
        self.serial.dtr = False
        self.serial.rts = False
        self.stop = threading.Event()
        self.thread: threading.Thread | None = None

    def open(self):
        self.serial.open()
        self.thread = threading.Thread(target=self._read, name="orcsdr-serial", daemon=True)
        self.thread.start()

    def _read(self):
        with self.raw_path.open("a", encoding="utf-8") as stream:
            while not self.stop.is_set():
                line = self.serial.readline().decode("utf-8", errors="replace").strip()
                if not line:
                    continue
                stream.write(f"{utc_now()} {line}\n")
                stream.flush()
                parsed = parse_orcsdr_line(line)
                if parsed:
                    self.events.add("orcsdr", **parsed)

    def command(self, command: str):
        self.serial.write((command + "\n").encode("ascii"))
        self.serial.flush()

    def wait_for(self, line_type: str, after: float, timeout: float = 10) -> dict:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for record in self.events.snapshot():
                if (
                    record["monotonic"] >= after
                    and record.get("line_type") == line_type
                ):
                    return record
            time.sleep(0.05)
        raise TimeoutError(f"{self.port} did not answer {line_type} within {timeout}s")

    def query(self, command: str, line_type: str) -> dict:
        started = time.monotonic()
        self.command(command)
        return self.wait_for(line_type, started)

    def close(self):
        self.stop.set()
        if self.thread:
            self.thread.join(timeout=2)
        if self.serial.is_open:
            self.serial.close()


def enum_name(enum_wrapper, value: int) -> str:
    try:
        return enum_wrapper.Name(value)
    except ValueError:
        return f"UNKNOWN_{value}"


def packet_id(packet) -> int | None:
    if isinstance(packet, dict):
        return packet.get("id")
    return getattr(packet, "id", None)


def mesh_snapshot(interface) -> dict:
    from meshtastic.protobuf import config_pb2, mesh_pb2

    lora = interface.localNode.localConfig.lora
    metadata = interface.metadata
    hardware = getattr(metadata, "hw_model", 0)
    return {
        "port": interface.devPath,
        "node_id": f"!{int(interface.myInfo.my_node_num):08x}",
        "firmware_version": str(getattr(metadata, "firmware_version", "unknown")),
        "hardware_model": enum_name(mesh_pb2.HardwareModel, int(hardware)),
        "region": enum_name(config_pb2.Config.LoRaConfig.RegionCode, int(lora.region)),
        "region_value": int(lora.region),
        "modem_preset": enum_name(
            config_pb2.Config.LoRaConfig.ModemPreset, int(lora.modem_preset)
        ),
        "modem_preset_value": int(lora.modem_preset),
        "use_preset": bool(lora.use_preset),
        "channel_num": int(lora.channel_num),
        "override_frequency_mhz": float(lora.override_frequency),
        "tx_power_dbm": int(lora.tx_power),
        "primary_channel_name": interface.localNode.channels[0].settings.name or "LongFast",
    }


def validate_pair(tx, reference, transport: str, ota_confirmed: bool):
    tx_lora = tx.localNode.localConfig.lora
    ref_lora = reference.localNode.localConfig.lora
    for label, lora in (("transmitter", tx_lora), ("reference", ref_lora)):
        if not lora.use_preset or int(lora.region) != 1 or int(lora.modem_preset) != 0:
            raise RuntimeError(f"{label} must already be configured for US/LONG_FAST")
        if int(lora.channel_num) != 0 or float(lora.override_frequency) != 0:
            raise RuntimeError(f"{label} must use the automatic US LongFast channel")
    tx_channel = tx.localNode.channels[0].settings
    ref_channel = reference.localNode.channels[0].settings
    if tx_channel.name != ref_channel.name or bytes(tx_channel.psk) != bytes(ref_channel.psk):
        raise RuntimeError("transmitter and reference primary channels do not match")
    if int(tx.myInfo.my_node_num) == int(reference.myInfo.my_node_num):
        raise RuntimeError("transmitter and reference must be different nodes")
    if transport == "OTA" and not ota_confirmed:
        raise RuntimeError("OTA runs require --confirm-local-ota-legal")


def correlate(slots: list[dict], events: list[dict], reference_tokens: set[str], window: float):
    captures = [
        event
        for event in events
        if event.get("kind") == "orcsdr"
        and event.get("line_type") == "RTL_IQ_START"
        and event.get("mode") == "energy"
    ]
    decodes = [
        event
        for event in events
        if event.get("kind") == "orcsdr"
        and event.get("line_type") == "RTL_LORA_NATIVE_DONE"
    ]
    decodes_by_sequence = {event.get("sequence"): event for event in decodes}
    rows = []
    for index, slot in enumerate(slots):
        end = slots[index + 1]["monotonic"] if index + 1 < len(slots) else slot["monotonic"] + window
        matched_captures = [event for event in captures if slot["monotonic"] <= event["monotonic"] < end]
        matched_decodes = [
            decodes_by_sequence[event.get("sequence")]
            for event in matched_captures
            if event.get("sequence") in decodes_by_sequence
        ]
        best = matched_decodes[0] if matched_decodes else {}
        best_capture = next(
            (event for event in matched_captures if event.get("sequence") == best.get("sequence")),
            None,
        )
        rows.append(
            {
                "slot_index": slot["slot_index"],
                "slot_type": slot["slot_type"],
                "sequence": slot.get("sequence"),
                "token": slot.get("token"),
                "packet_id": slot.get("packet_id"),
                "reference_received": (
                    slot["token"] in reference_tokens if slot["slot_type"] == "tx" else None
                ),
                "orcsdr_rf_events": len(matched_captures),
                "orcsdr_decode_attempts": len(matched_decodes),
                "orcsdr_preambles": sum(int(item.get("preambles", 0)) for item in matched_decodes),
                "orcsdr_header_failures": sum(int(item.get("header_failures", 0)) for item in matched_decodes),
                "orcsdr_crc_ok": sum(int(item.get("crc_ok", 0)) for item in matched_decodes),
                "orcsdr_packets": sum(int(item.get("packets", 0)) for item in matched_decodes),
                "orcsdr_zero_preamble_attempts": sum(
                    int(item.get("preambles", 0)) == 0 for item in matched_decodes
                ),
                "extra_decode_attempts": max(0, len(matched_decodes) - 1),
                "decode_latency_seconds": (
                    round(best["monotonic"] - best_capture["monotonic"], 3)
                    if best_capture else None
                ),
            }
        )
    return rows


def quiet_trigger_counts(events: list[dict]) -> tuple[int, int, int]:
    captures = [
        event for event in events
        if event.get("kind") == "orcsdr"
        and event.get("line_type") == "RTL_IQ_START"
        and event.get("mode") == "energy"
    ]
    decodes = {
        event.get("sequence"): event for event in events
        if event.get("kind") == "orcsdr"
        and event.get("line_type") == "RTL_LORA_NATIVE_DONE"
    }
    false_triggers = sum(
        capture.get("sequence") in decodes
        and int(decodes[capture.get("sequence")].get("preambles", 0)) == 0
        for capture in captures
    )
    unresolved = sum(capture.get("sequence") not in decodes for capture in captures)
    return len(captures), false_triggers, unresolved


def quiet_window_events(events: list[dict], start: float, end: float) -> list[dict]:
    captures = [
        event for event in events
        if event.get("line_type") == "RTL_IQ_START"
        and start <= event["monotonic"] <= end
    ]
    sequences = {event.get("sequence") for event in captures}
    return captures + [
        event for event in events
        if event.get("line_type") == "RTL_LORA_NATIVE_DONE"
        and event.get("sequence") in sequences
    ]


def nearest_rank(values: list[float], percentile: float) -> float | None:
    ordered = sorted(values)
    return ordered[max(0, math.ceil(len(ordered) * percentile) - 1)] if ordered else None


def write_results(
    run_dir: Path, rows: list[dict], quiet_events: list[dict], all_events: list[dict], duration: float
):
    fields = list(rows[0]) if rows else ["sequence", "token"]
    with (run_dir / "results.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)
    quiet_capture_count, false_triggers, unresolved_quiet = quiet_trigger_counts(quiet_events)
    tx_rows = [row for row in rows if row["slot_type"] == "tx"]
    control_rows = [row for row in rows if row["slot_type"] == "control"]
    requested = len(tx_rows)
    ratio = lambda count: round(count / requested, 4) if requested else None
    control_ratio = lambda count: round(count / len(control_rows), 4) if control_rows else None
    reference_received = sum(row["reference_received"] for row in tx_rows)
    rf_detected = sum(row["orcsdr_rf_events"] > 0 for row in tx_rows)
    preamble_detected = sum(row["orcsdr_preambles"] > 0 for row in tx_rows)
    header_succeeded = sum(
        row["orcsdr_preambles"] > row["orcsdr_header_failures"] for row in tx_rows
    )
    crc_succeeded = sum(row["orcsdr_crc_ok"] > 0 for row in tx_rows)
    mesh_decoded = sum(row["orcsdr_packets"] > 0 for row in tx_rows)
    extra_decode_attempts = sum(row["extra_decode_attempts"] for row in tx_rows)
    control_rf = sum(row["orcsdr_rf_events"] > 0 for row in control_rows)
    control_preambles = sum(row["orcsdr_preambles"] > 0 for row in control_rows)
    control_crc = sum(row["orcsdr_crc_ok"] > 0 for row in control_rows)
    control_zero_preamble = sum(row["orcsdr_zero_preamble_attempts"] for row in control_rows)
    latencies = sorted(
        row["decode_latency_seconds"] for row in tx_rows
        if row["decode_latency_seconds"] is not None
    )
    results = {
        "tx_requested": requested,
        "reference_received": reference_received,
        "reference_receiver_success_rate": ratio(reference_received),
        "orcsdr_rf_detected": rf_detected,
        "rf_detection_rate": ratio(rf_detected),
        "orcsdr_preamble_detected": preamble_detected,
        "preamble_detection_rate": ratio(preamble_detected),
        "orcsdr_header_success": header_succeeded,
        "header_success_rate": ratio(header_succeeded),
        "orcsdr_crc_success": crc_succeeded,
        "crc_success_rate": ratio(crc_succeeded),
        "orcsdr_meshtastic_decoded": mesh_decoded,
        "meshtastic_decode_rate": ratio(mesh_decoded),
        "extra_decode_attempts": extra_decode_attempts,
        "extra_decode_attempt_rate": ratio(extra_decode_attempts),
        "duplicate_rate": None,
        "control_slots": len(control_rows),
        "control_slots_with_rf": control_rf,
        "control_rf_rate": control_ratio(control_rf),
        "control_slots_with_preamble": control_preambles,
        "control_preamble_rate": control_ratio(control_preambles),
        "control_slots_with_crc": control_crc,
        "control_crc_rate": control_ratio(control_crc),
        "control_zero_preamble_attempts": control_zero_preamble,
        "average_decode_latency_seconds": round(sum(latencies) / len(latencies), 3) if latencies else None,
        "p95_decode_latency_seconds": nearest_rank(latencies, 0.95),
        "quiet_seconds": duration,
        "quiet_rf_captures": quiet_capture_count,
        "false_triggers": false_triggers,
        "unresolved_quiet_captures": unresolved_quiet,
        "false_triggers_per_minute": round(false_triggers * 60 / duration, 3) if duration else None,
        "receiver_blind_time_seconds": None,
        "usb_capture_drops": sum(
            event.get("line_type") == "RTL_LORA_CAPTURE_DROP"
            for event in all_events
            if event.get("kind") == "orcsdr"
        ),
        "memory_behavior": "not exposed by the current LoRa serial status",
        "correlation": "host monotonic time window; payload-level OrcSDR correlation not yet exposed",
        "warnings": [
            "Receiver blind time is not claimed from decoder elapsed time alone.",
            "Heap and PSRAM deltas are unavailable in the current LoRa status output.",
            "Duplicate packet decodes cannot be claimed without OrcSDR payload identity.",
        ],
    }
    (run_dir / "results.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    lines = [
        "# LoRa baseline run",
        "",
        f"- TX requested: {requested}",
        f"- Reference RX: {results['reference_received']}/{requested} ({results['reference_receiver_success_rate']})",
        f"- OrcSDR RF detections: {results['orcsdr_rf_detected']}/{requested} ({results['rf_detection_rate']})",
        f"- OrcSDR preamble detections: {results['orcsdr_preamble_detected']}/{requested} ({results['preamble_detection_rate']})",
        f"- OrcSDR header successes: {results['orcsdr_header_success']}/{requested} ({results['header_success_rate']})",
        f"- OrcSDR CRC successes: {results['orcsdr_crc_success']}/{requested} ({results['crc_success_rate']})",
        f"- OrcSDR Meshtastic decodes: {results['orcsdr_meshtastic_decoded']}/{requested} ({results['meshtastic_decode_rate']})",
        f"- Extra decode attempts in TX windows: {results['extra_decode_attempts']}",
        "- Duplicate packet rate: not measurable without OrcSDR payload identity",
        f"- Control slots with RF: {results['control_slots_with_rf']}/{results['control_slots']}",
        f"- Control slots with preambles: {results['control_slots_with_preamble']}/{results['control_slots']}",
        f"- Control slots with CRC: {results['control_slots_with_crc']}/{results['control_slots']}",
        f"- Quiet-window false triggers: {false_triggers} in {duration:.1f}s",
        f"- Quiet captures without a completed decode: {unresolved_quiet}",
        "- Correlation boundary: OrcSDR events are associated by a non-overlapping host-time window.",
    ]
    (run_dir / "summary.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    return results


def self_check():
    sample = parse_orcsdr_line(
        "RTL_LORA_NATIVE_DONE packets=1 preambles=2 header_failures=0 crc_ok=1 "
        "crc_failures=0 elapsed_ms=44 sequence=7"
    )
    assert sample and sample["crc_ok"] == 1 and sample["sequence"] == 7
    slots = [
        {"slot_index": 1, "slot_type": "tx", "sequence": 1, "token": TOKEN_PREFIX + "000001", "packet_id": 9, "monotonic": 10.0},
        {"slot_index": 2, "slot_type": "control", "monotonic": 20.0},
    ]
    events = [
        {"kind": "orcsdr", "line_type": "RTL_IQ_START", "mode": "energy", "sequence": 7, "monotonic": 10.1},
        {"kind": "orcsdr", "line_type": "RTL_LORA_NATIVE_DONE", "sequence": 7, "preambles": 1, "crc_ok": 1, "packets": 1, "monotonic": 10.8},
    ]
    row, control = correlate(slots, events, {TOKEN_PREFIX + "000001"}, 8)
    assert row["reference_received"] and row["orcsdr_crc_ok"] == 1
    assert row["decode_latency_seconds"] == 0.7 and row["extra_decode_attempts"] == 0
    assert control["reference_received"] is None and control["orcsdr_rf_events"] == 0
    assert nearest_rank(list(range(1, 11)), 0.95) == 10
    assert quiet_trigger_counts(events) == (1, 0, 0)
    assert quiet_trigger_counts(quiet_window_events(events, 10.0, 10.2)) == (1, 0, 0)
    events[-1]["preambles"] = 0
    assert quiet_trigger_counts(events) == (1, 1, 0)
    print("lora_lab self-check: PASS")


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tx-port")
    parser.add_argument("--reference-port")
    parser.add_argument("--orcsdr-port")
    parser.add_argument("--count", type=int, default=10)
    parser.add_argument("--control-count", type=int, default=0)
    parser.add_argument("--schedule-seed", type=int, default=90210)
    parser.add_argument("--interval-seconds", type=float, default=8)
    parser.add_argument("--settle-seconds", type=float, default=20)
    parser.add_argument("--quiet-seconds", type=float, default=180)
    parser.add_argument("--transport", choices=("OTA", "SHIELDED_RF", "CABLED_RF"), default="OTA")
    parser.add_argument("--confirm-local-ota-legal", action="store_true")
    parser.add_argument("--output-root", type=Path, default=Path("artifacts/lora_validation"))
    parser.add_argument("--inventory-only", action="store_true")
    parser.add_argument("--quiet-only", action="store_true")
    parser.add_argument("--self-check", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.self_check:
        self_check()
        return 0
    if (
        args.count < 1 or args.control_count < 0 or args.interval_seconds < 4
        or args.quiet_seconds < 0 or args.settle_seconds < 0
    ):
        raise SystemExit("count must be positive; control, quiet, and settle must be nonnegative; interval >= 4s")
    run_dir = args.output_root / datetime.now().strftime("%Y%m%d-%H%M%S")
    run_dir.mkdir(parents=True)
    inventory = {
        "created": utc_now(),
        "serial_devices": serial_inventory(),
        "meshtastic_python": importlib.metadata.version("meshtastic"),
        "pyserial": importlib.metadata.version("pyserial"),
    }
    (run_dir / "device_inventory.json").write_text(json.dumps(inventory, indent=2) + "\n", encoding="utf-8")
    print(f"Evidence directory: {run_dir}")
    if args.inventory_only:
        from meshtastic.serial_interface import SerialInterface

        inventory["identified"] = {}
        for role, port in (("transmitter", args.tx_port), ("reference_receiver", args.reference_port)):
            if not port:
                continue
            interface = None
            try:
                interface = SerialInterface(devPath=port, noNodes=True, timeout=30)
                inventory["identified"][role] = mesh_snapshot(interface)
            except Exception as error:
                inventory["identified"][role] = {"port": port, "error": str(error)}
            finally:
                if interface:
                    interface.close()
        if args.orcsdr_port:
            probe_events = EventLog(run_dir / "events.jsonl")
            probe = OrcConsole(args.orcsdr_port, run_dir / "orcsdr.log", probe_events)
            try:
                probe.open()
                inventory["identified"]["orcsdr"] = {
                    "port": args.orcsdr_port,
                    "iq_status": probe.query("RTL_IQ_STATUS", "RTL_IQ_STATUS"),
                    "plan_status": probe.query("RTL_LORA_PLAN_STATUS", "RTL_LORA_PLAN_STATUS"),
                    "frequency_status": probe.query("RTL_FREQ", "RTL_FREQ_STATUS"),
                }
            except Exception as error:
                inventory["identified"]["orcsdr"] = {
                    "port": args.orcsdr_port,
                    "error": str(error),
                }
            finally:
                probe.close()
        (run_dir / "device_inventory.json").write_text(
            json.dumps(inventory, indent=2) + "\n", encoding="utf-8"
        )
        print(json.dumps(inventory, indent=2))
        return 0
    if args.quiet_only:
        if not args.orcsdr_port:
            raise SystemExit("quiet-only runs require --orcsdr-port")
        events = EventLog(run_dir / "events.jsonl")
        orc = OrcConsole(args.orcsdr_port, run_dir / "orcsdr.log", events)
        try:
            orc.open()
            iq_status = orc.query("RTL_IQ_STATUS", "RTL_IQ_STATUS")
            plan_status = orc.query("RTL_LORA_PLAN_STATUS", "RTL_LORA_PLAN_STATUS")
            frequency_status = orc.query("RTL_FREQ", "RTL_FREQ_STATUS")
            if iq_status.get("auto") != "on" or frequency_status.get("band") != "LORA":
                raise RuntimeError("OrcSDR must already be listening with automatic LoRa capture")
            (run_dir / "configuration.json").write_text(
                json.dumps(
                    {
                        "transport": "RX_ONLY",
                        "orcsdr": {
                            "port": args.orcsdr_port,
                            "iq_status": iq_status,
                            "plan_status": plan_status,
                            "frequency_status": frequency_status,
                        },
                    },
                    indent=2,
                ) + "\n",
                encoding="utf-8",
            )
            quiet_start = time.monotonic()
            events.add("quiet_start", seconds=args.quiet_seconds)
            time.sleep(args.quiet_seconds)
            quiet_end = time.monotonic()
            all_events = events.snapshot()
            quiet_events = quiet_window_events(all_events, quiet_start, quiet_end)
            results = write_results(
                run_dir, [], quiet_events, all_events, quiet_end - quiet_start
            )
            events.add("suite_complete", results=results)
            print(json.dumps(results, indent=2))
            return 0
        finally:
            orc.close()
    if not all((args.tx_port, args.reference_port, args.orcsdr_port)):
        raise SystemExit("full runs require --tx-port, --reference-port, and --orcsdr-port")

    from meshtastic.serial_interface import SerialInterface
    from pubsub import pub

    events = EventLog(run_dir / "events.jsonl")
    reference_tokens: set[str] = set()
    tx_log = (run_dir / "tx.log").open("a", encoding="utf-8")
    reference_log = (run_dir / "reference_rx.log").open("a", encoding="utf-8")
    interfaces = {}

    def on_receive(packet, interface=None):
        decoded = packet.get("decoded", {})
        text = decoded.get("text")
        if not isinstance(text, str) or not text.startswith(TOKEN_PREFIX):
            return
        port = getattr(interface, "devPath", "unknown")
        record = events.add(
            "mesh_rx", port=port, token=text, sender=packet.get("from"),
            packet_id=packet.get("id"), via_mqtt=bool(packet.get("viaMqtt", False)),
        )
        if port == args.reference_port and not record["via_mqtt"]:
            reference_tokens.add(text)
            reference_log.write(json.dumps(record, separators=(",", ":")) + "\n")
            reference_log.flush()

    orc = OrcConsole(args.orcsdr_port, run_dir / "orcsdr.log", events)
    slots = []
    pub.subscribe(on_receive, "meshtastic.receive")
    try:
        orc.open()
        iq_status = orc.query("RTL_IQ_STATUS", "RTL_IQ_STATUS")
        plan_status = orc.query("RTL_LORA_PLAN_STATUS", "RTL_LORA_PLAN_STATUS")
        frequency_status = orc.query("RTL_FREQ", "RTL_FREQ_STATUS")
        if iq_status.get("storage") != "psram" or iq_status.get("auto") != "on":
            raise RuntimeError("OrcSDR is not an active automatic LoRa PSRAM receiver")
        if plan_status.get("region") != "US" or plan_status.get("profile") != "LONGFAST":
            raise RuntimeError("OrcSDR must already be on US/LONGFAST")
        if (
            frequency_status.get("band") != "LORA"
            or frequency_status.get("frequency_hz") != plan_status.get("frequency_hz")
        ):
            raise RuntimeError("OrcSDR must already be listening on its selected LoRa channel")

        interfaces["reference"] = SerialInterface(devPath=args.reference_port, timeout=60)
        interfaces["tx"] = SerialInterface(devPath=args.tx_port, timeout=60)
        validate_pair(interfaces["tx"], interfaces["reference"], args.transport, args.confirm_local_ota_legal)
        schedule = ["tx"] * args.count + ["control"] * args.control_count
        random.Random(args.schedule_seed).shuffle(schedule)
        configuration = {
            "transport": args.transport,
            "ota_legality_confirmed_by_operator": args.confirm_local_ota_legal,
            "schedule_seed": args.schedule_seed,
            "schedule": schedule,
            "slot_seconds": args.interval_seconds,
            "transmitter": mesh_snapshot(interfaces["tx"]),
            "reference_receiver": mesh_snapshot(interfaces["reference"]),
            "orcsdr": {
                "port": args.orcsdr_port,
                "iq_status": iq_status,
                "plan_status": plan_status,
                "frequency_status": frequency_status,
            },
        }
        if configuration["transmitter"]["hardware_model"] != "HELTEC_V4":
            raise RuntimeError("the controlled transmitter must identify as HELTEC_V4")
        (run_dir / "configuration.json").write_text(json.dumps(configuration, indent=2) + "\n", encoding="utf-8")

        quiet_start = time.monotonic()
        events.add("quiet_start", seconds=args.quiet_seconds)
        time.sleep(args.quiet_seconds)
        quiet_end = time.monotonic()
        events.add("quiet_end")

        tx_interface = interfaces["tx"]
        sequence = 0
        for slot_index, slot_type in enumerate(schedule, 1):
            slot_start = time.monotonic()
            if slot_type == "tx":
                sequence += 1
                token = f"{TOKEN_PREFIX}{sequence:06d}"
                packet = tx_interface.sendText(token, destinationId="^all", wantAck=False, channelIndex=0)
                record = events.add(
                    "mesh_tx", slot_index=slot_index, slot_type=slot_type,
                    sequence=sequence, token=token, packet_id=packet_id(packet),
                    transport=args.transport, monotonic=slot_start,
                )
                tx_log.write(json.dumps(record, separators=(",", ":")) + "\n")
                tx_log.flush()
            else:
                record = events.add(
                    "control_slot", slot_index=slot_index, slot_type=slot_type,
                    monotonic=slot_start,
                )
            slots.append(record)
            time.sleep(max(0, slot_start + args.interval_seconds - time.monotonic()))
        time.sleep(args.settle_seconds)
        all_events = events.snapshot()
        quiet_events = quiet_window_events(all_events, quiet_start, quiet_end)
        rows = correlate(slots, all_events, reference_tokens, args.interval_seconds)
        results = write_results(run_dir, rows, quiet_events, all_events, quiet_end - quiet_start)
        events.add("suite_complete", results=results)
        print(json.dumps(results, indent=2))
        return 0
    finally:
        pub.unsubscribe(on_receive, "meshtastic.receive")
        for interface in reversed(list(interfaces.values())):
            interface.close()
        orc.close()
        tx_log.close()
        reference_log.close()


if __name__ == "__main__":
    raise SystemExit(main())
