#!/usr/bin/env python3
"""Controlled two-node Meshtastic/RTL-SDR RF characterization suite."""

from __future__ import annotations

import argparse
import base64
import csv
import json
import re
import subprocess
import sys
import threading
import time
import uuid
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

import decode_orciq


LOCAL_ID = "!f66f81bc"
REMOTE_ID = "!f670a224"
LOCAL_NUM = int(LOCAL_ID[1:], 16)
REMOTE_NUM = int(REMOTE_ID[1:], 16)
PUBLIC_HZ = 906_875_000
ISOLATED_HZ = 907_875_000
SUMMARY_FIELDS = (
    "timestamp", "phase", "case", "frequency_hz", "tx_power", "payload_length",
    "expected_from", "expected_to", "decoded_from", "decoded_to", "packet_id",
    "port", "crc", "cfo_hz", "signal_dbfs", "noise_dbfs", "clipping_pct",
    "transfer_seconds", "ack_latency_seconds", "capture", "result",
)


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def packet_id(packet) -> int | None:
    if packet is None:
        return None
    if isinstance(packet, dict):
        return packet.get("id")
    return getattr(packet, "id", None)


def safe_packet(packet: dict) -> dict:
    decoded = packet.get("decoded", {})
    payload = decoded.get("payload")
    if isinstance(payload, bytes):
        payload = payload.hex()
    return {
        "from": packet.get("from"),
        "to": packet.get("to"),
        "id": packet.get("id"),
        "via_mqtt": bool(packet.get("viaMqtt", packet.get("via_mqtt", False))),
        "port": decoded.get("portnum"),
        "request_id": decoded.get("requestId"),
        "payload_hex": payload,
    }


@dataclass
class CaptureEvidence:
    path: Path
    decoded: list[dict]
    signal_dbfs: float | None
    noise_dbfs: float | None
    clipping_pct: float
    transfer_seconds: float


class Evidence:
    def __init__(self, root: Path):
        self.root = root
        self.captures = root / "captures"
        self.captures.mkdir(parents=True)
        self.events_path = root / "events.jsonl"
        self.summary_path = root / "summary.csv"
        self.summary_file = self.summary_path.open("w", newline="", encoding="utf-8")
        self.summary = csv.DictWriter(self.summary_file, fieldnames=SUMMARY_FIELDS)
        self.summary.writeheader()
        self.lock = threading.Lock()

    def event(self, kind: str, **fields):
        record = {"timestamp": utc_now(), "kind": kind, **fields}
        with self.lock, self.events_path.open("a", encoding="utf-8") as stream:
            stream.write(json.dumps(record, separators=(",", ":"), default=str) + "\n")

    def row(self, **fields):
        record = {key: fields.get(key, "") for key in SUMMARY_FIELDS}
        with self.lock:
            self.summary.writerow(record)
            self.summary_file.flush()

    def close(self):
        self.summary_file.close()


class RfSuite:
    def __init__(self, rtl_port: str, mesh_port: str, output: Path):
        self.rtl_port = rtl_port
        self.mesh_port = mesh_port
        self.evidence = Evidence(output)
        self.rtl = None
        self.mesh = None
        self.primary_psk: bytes | None = None
        self.local_private_key: bytes | None = None
        self.remote_public_key: bytes | None = None
        self.original = None
        self.rx_packets: list[tuple[float, dict]] = []
        self.rx_lock = threading.Lock()
        self.faults: list[str] = []
        self.last_signal = None
        self.last_noise = None
        self.capture_index = 0
        self.current_power = None
        self.restored = False

    def _rx(self, packet, interface=None):
        clean = safe_packet(packet)
        if clean["via_mqtt"]:
            self.evidence.event("rx_excluded_mqtt", packet=clean)
            return
        with self.rx_lock:
            self.rx_packets.append((time.monotonic(), packet))
        self.evidence.event("mesh_rx", packet=clean)

    def connect(self):
        from pubsub import pub
        from meshtastic.serial_interface import SerialInterface

        try:
            self.rtl = decode_orciq._open_serial(self.rtl_port)
        except Exception as error:
            raise RuntimeError(
                f"Cannot open {self.rtl_port}; stop monitor_lora_to_pc.bat first: {error}"
            ) from error
        self.rtl.write(b"RTL_IQ_STATUS\n")
        status = decode_orciq._wait_line(self.rtl, ("RTL_IQ_STATUS",), 10)
        if "storage=psram" not in status:
            raise RuntimeError(f"{self.rtl_port} is not the LoRa PSRAM test build: {status}")
        self.evidence.event("rtl_identity", port=self.rtl_port, status=status)

        pub.subscribe(self._rx, "meshtastic.receive")
        self.mesh = SerialInterface(devPath=self.mesh_port, timeout=60)
        actual = int(self.mesh.myInfo.my_node_num)
        if actual != LOCAL_NUM:
            raise RuntimeError(f"{self.mesh_port} is !{actual:08x}, expected {LOCAL_ID}")
        node = self.mesh.localNode
        lora = node.localConfig.lora
        if int(lora.region) != 1 or int(lora.modem_preset) != 0 or not lora.use_preset:
            raise RuntimeError("Hardcore must use US region and LONG_FAST preset")
        primary = node.channels[0]
        if primary.settings.name not in ("", "LongFast"):
            raise RuntimeError(f"primary channel is {primary.settings.name!r}, expected LongFast")
        self.primary_psk = bytes(primary.settings.psk)
        if not self.primary_psk:
            raise RuntimeError("primary LongFast PSK is unavailable")
        self.local_private_key = bytes(node.localConfig.security.private_key)
        remote_user = self.mesh.nodes.get(REMOTE_ID, {}).get("user", {})
        try:
            self.remote_public_key = base64.b64decode(
                remote_user.get("publicKey", ""), validate=True
            )
        except ValueError as error:
            raise RuntimeError("Kushcore public key is invalid in Hardcore's NodeDB") from error
        if len(self.local_private_key) != 32 or len(self.remote_public_key) != 32:
            raise RuntimeError("Hardcore/Kushcore PKI key material is unavailable")
        mqtt = node.moduleConfig.mqtt
        self.original = {
            "channel_num": int(lora.channel_num),
            "override_frequency": float(lora.override_frequency),
            "tx_power": int(lora.tx_power),
            "ignore_mqtt": bool(lora.ignore_mqtt),
            "mqtt_enabled": bool(mqtt.enabled),
            "mqtt_proxy": bool(mqtt.proxy_to_client_enabled),
        }
        metadata = getattr(self.mesh, "metadata", None)
        firmware = getattr(metadata, "firmware_version", "unknown")
        hardware = getattr(metadata, "hw_model", "unknown")
        if isinstance(hardware, int):
            from meshtastic.protobuf.mesh_pb2 import HardwareModel
            hardware = HardwareModel.Name(hardware)
        manifest = {
            "created": utc_now(),
            "git_commit": subprocess.run(
                ["git", "rev-parse", "HEAD"], capture_output=True, text=True, check=True
            ).stdout.strip(),
            "rtl_port": self.rtl_port,
            "mesh_port": self.mesh_port,
            "hardcore": {"node_id": LOCAL_ID, "hardware": str(hardware), "firmware": firmware},
            "kushcore": {"node_id": REMOTE_ID},
            "region": "US",
            "preset": "LONG_FAST",
            "frequencies_hz": {"public": PUBLIC_HZ, "isolated_slot_24": ISOLATED_HZ},
            "power_levels": [2, 15, 30],
            "psk_retained": False,
            "pki_key_material_retained": False,
        }
        (self.evidence.root / "manifest.json").write_text(
            json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
        )
        self.evidence.event(
            "mesh_identity", port=self.mesh_port, node_id=LOCAL_ID,
            remote_required=REMOTE_ID, firmware=firmware, hardware=str(hardware),
        )

    def set_lora(self, *, slot: int | None = None, power: int | None = None,
                 override: float | None = None, ignore_mqtt: bool | None = None):
        lora = self.mesh.localNode.localConfig.lora
        if slot is not None:
            lora.channel_num = slot
        if power is not None:
            lora.tx_power = power
            self.current_power = power
        if override is not None:
            lora.override_frequency = override
        if ignore_mqtt is not None:
            lora.ignore_mqtt = ignore_mqtt
        self.mesh.localNode.writeConfig("lora")
        self.evidence.event(
            "config_lora", slot=int(lora.channel_num), power=int(lora.tx_power),
            override_frequency=float(lora.override_frequency), ignore_mqtt=bool(lora.ignore_mqtt),
        )
        time.sleep(2)

    def set_mqtt_disabled(self):
        mqtt = self.mesh.localNode.moduleConfig.mqtt
        mqtt.enabled = False
        mqtt.proxy_to_client_enabled = False
        self.mesh.localNode.writeConfig("mqtt")
        self.evidence.event("config_mqtt", enabled=False, proxy_to_client_enabled=False)
        time.sleep(2)

    def tune_rtl(self, frequency_hz: int):
        self.rtl.write(f"RTL_LORA_TUNE {frequency_hz}\n".encode("ascii"))
        line = decode_orciq._wait_line(
            self.rtl, ("RTL_LORA_TUNE_OK", "RTL_LORA_TUNE_ERROR"), 10
        )
        if line != f"RTL_LORA_TUNE_OK frequency_hz={frequency_hz}":
            raise RuntimeError(line)
        self.evidence.event("rtl_tune", frequency_hz=frequency_hz)

    def _observe_line(self, line: str):
        if line.startswith(("RTL_STOP", "RTL_IQ_RETRIEVE_RESUMING", "RTL_INIT")):
            self.faults.append(line)
            self.evidence.event("rtl_fault", line=line)
        energy = re.search(r"(?:signal|level)_dbfs=(-?[\d.]+).*noise_dbfs=(-?[\d.]+)", line)
        if energy:
            self.last_signal = float(energy.group(1))
            self.last_noise = float(energy.group(2))
            self.evidence.event(
                "rtl_energy", signal_dbfs=self.last_signal, noise_dbfs=self.last_noise
            )

    def retrieve_ready_capture(self, phase: str, case: str) -> CaptureEvidence:
        started = time.monotonic()
        remote_name, content = decode_orciq._retrieve_latest_iq(
            self.rtl,
            retry_callback=lambda attempt, error: self.evidence.event(
                "capture_retry", phase=phase, case=case, attempt=attempt, error=error
            ),
        )
        transfer_seconds = time.monotonic() - started
        self.capture_index += 1
        path = self.evidence.captures / f"{self.capture_index:03d}_{phase}_{case}_{Path(remote_name).name}"
        path.write_bytes(content)
        _, _, _, _, raw = decode_orciq.read_capture(path)
        clipped = sum(value <= 1 or value >= 254 for value in raw)
        clipping_pct = 100.0 * clipped / len(raw)
        try:
            decoded = decode_orciq.decode_capture(
                path,
                channel_psks=[("primary-memory", self.primary_psk)],
                pki_keys=[(
                    "hardcore-kushcore-memory", self.local_private_key,
                    self.remote_public_key,
                )],
            )
        except ValueError as error:
            decoded = [{"error": str(error), "index": 0}]
            self.evidence.event(
                "capture_decode_failed", phase=phase, case=case, error=str(error)
            )
        self.rtl.write(b"RTL_IQ_RETRIEVE_END\n")
        released = decode_orciq._wait_line(
            self.rtl, ("RTL_IQ_RETRIEVE_DONE", "RTL_IQ_RETRIEVE_RESUMING"), 15
        )
        self._observe_line(released)
        self.evidence.event(
            "capture_verified", phase=phase, case=case, path=str(path),
            bytes=len(content), transfer_seconds=transfer_seconds,
            clipping_pct=clipping_pct, decoded_packets=len(decoded), release=released,
        )
        return CaptureEvidence(
            path, decoded, self.last_signal, self.last_noise, clipping_pct, transfer_seconds
        )

    def drain_pending(self, phase: str, case: str):
        for _ in range(20):
            self.rtl.write(b"RTL_IQ_STATUS\n")
            status = decode_orciq._wait_line(self.rtl, ("RTL_IQ_STATUS",), 10)
            if "ready=true" in status:
                self.evidence.event("stale_capture_drain", phase=phase, case=case)
                self.retrieve_ready_capture(phase, case)
                continue
            if "active=true" not in status:
                return
            time.sleep(0.5)
        raise TimeoutError("pending ambient IQ capture did not finish")

    def wait_capture(self, phase: str, case: str, timeout: float = 30) -> CaptureEvidence:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            line = self.rtl.readline().decode("utf-8", errors="replace").strip()
            if not line:
                continue
            self._observe_line(line)
            if decode_orciq.IQ_DONE_RE.match(line):
                return self.retrieve_ready_capture(phase, case)
        self.rtl.write(b"RTL_IQ_STATUS\n")
        status = decode_orciq._wait_line(self.rtl, ("RTL_IQ_STATUS",), 10)
        if "ready=true" in status:
            self.evidence.event("capture_done_line_missed", phase=phase, case=case)
            return self.retrieve_ready_capture(phase, case)
        raise TimeoutError(f"no IQ capture for {phase}/{case} within {timeout}s")

    def ambient(self, seconds: int):
        self.evidence.event("ambient_start", seconds=seconds)
        self.drain_pending("preflight", "ambient_pending")
        deadline = time.monotonic() + seconds
        count = 0
        while time.monotonic() < deadline:
            line = self.rtl.readline().decode("utf-8", errors="replace").strip()
            if not line:
                continue
            self._observe_line(line)
            if decode_orciq.IQ_DONE_RE.match(line):
                self.retrieve_ready_capture("preflight", f"ambient_{count + 1}")
                count += 1
        self.evidence.event("ambient_done", captures=count)

    @staticmethod
    def exact_text(prefix: str, size: int) -> str:
        if len(prefix.encode("utf-8")) > size:
            raise ValueError("test prefix exceeds requested payload length")
        return prefix + ("x" * (size - len(prefix)))

    def run_text(self, phase: str, case: str, frequency: int, text: str,
                 destination: str, want_ack: bool, allow_ack_only: bool = False) -> bool:
        self.drain_pending(phase, case + "_preclear")
        ack = threading.Event()
        ack_at = [None]
        ack_packet = [None]
        sent_at = time.monotonic()

        def response(packet):
            ack_at[0] = time.monotonic()
            ack_packet[0] = packet
            ack.set()
            self.evidence.event("mesh_ack", request_id=packet_id(packet), packet=safe_packet(packet))

        sent = self.mesh.sendText(
            text, destinationId=destination, wantAck=want_ack,
            onResponse=response if want_ack else None, channelIndex=0,
        )
        sent_id = packet_id(sent)
        self.evidence.event(
            "mesh_tx", phase=phase, case=case, packet_id=sent_id, destination=destination,
            port="TEXT_MESSAGE_APP", payload_length=len(text.encode("utf-8")), token=text,
            tx_power=self.current_power,
        )
        capture = self.wait_capture(phase, case)
        if want_ack:
            ack.wait(20)
        ack_latency = ack_at[0] - sent_at if ack_at[0] else None
        expected_to = 0xFFFFFFFF if destination == "^all" else REMOTE_NUM
        matches = [
            item for item in capture.decoded
            if "error" not in item and item.get("from") == LOCAL_NUM
            and item.get("to") == expected_to and item.get("port") == 1
            and item.get("payload") == text.encode("utf-8")
        ]
        match = matches[0] if matches else {}
        rf_ack = next(
            (item for item in capture.decoded if "error" not in item
             and item.get("from") == REMOTE_NUM and item.get("to") == LOCAL_NUM
             and item.get("port") == 5 and item.get("request_id") == sent_id), {}
        )
        api_ack = safe_packet(ack_packet[0]) if ack_packet[0] else {}
        api_ack_ok = bool(
            api_ack and not api_ack["via_mqtt"] and api_ack["from"] == REMOTE_NUM
            and api_ack["to"] == LOCAL_NUM and api_ack["request_id"] == sent_id
        )
        ack_ok = not want_ack or (ack.is_set() and api_ack_ok)
        result = "PASS" if matches and ack_ok else "FAIL"
        if allow_ack_only and want_ack and ack.is_set() and api_ack_ok:
            result = "PASS_ACK_ONLY_OVERLOAD" if not matches else "PASS"
        elif capture.clipping_pct > 0.1:
            result = "OVERLOAD" if matches and ack_ok else "OVERLOAD_NO_DECODE"
        self.evidence.row(
            timestamp=utc_now(), phase=phase, case=case, frequency_hz=frequency,
            tx_power=self.current_power, payload_length=len(text.encode("utf-8")),
            expected_from=LOCAL_ID, expected_to=f"!{expected_to:08x}",
            decoded_from=f"!{match['from']:08x}" if match else "",
            decoded_to=f"!{match['to']:08x}" if match else "", packet_id=sent_id,
            port="TEXT_MESSAGE_APP", crc="VALID" if match else "",
            cfo_hz=match.get("cfo_hz", ""), signal_dbfs=capture.signal_dbfs,
            noise_dbfs=capture.noise_dbfs, clipping_pct=f"{capture.clipping_pct:.5f}",
            transfer_seconds=f"{capture.transfer_seconds:.3f}",
            ack_latency_seconds=f"{ack_latency:.3f}" if ack_latency is not None else "",
            capture=str(capture.path), result=result,
        )
        print(f"{phase}/{case}: {result}")
        return result in ("PASS", "OVERLOAD", "PASS_ACK_ONLY_OVERLOAD")

    def run_request(self, phase: str, case: str, frequency: int, port_num: int, message):
        self.drain_pending(phase, case + "_preclear")
        sent = self.mesh.sendData(
            message, destinationId=REMOTE_ID, portNum=port_num,
            wantAck=True, wantResponse=True, channelIndex=0,
        )
        sent_id = packet_id(sent)
        self.evidence.event(
            "mesh_tx", phase=phase, case=case, packet_id=sent_id,
            destination=REMOTE_ID, port=port_num, payload_length=0,
            tx_power=self.current_power,
        )
        capture = self.wait_capture(phase, case)
        outgoing = next(
            (item for item in capture.decoded if "error" not in item
             and item.get("from") == LOCAL_NUM and item.get("port") == int(port_num)), {}
        )
        rf_response = next(
            (item for item in capture.decoded if "error" not in item
             and item.get("from") == REMOTE_NUM and item.get("request_id") == sent_id), {}
        )
        result = "PASS" if outgoing and rf_response else "NOT_SUPPORTED" if outgoing else "FAIL"
        self.evidence.row(
            timestamp=utc_now(), phase=phase, case=case, frequency_hz=frequency,
            tx_power=self.current_power, payload_length=0, expected_from=LOCAL_ID,
            expected_to=REMOTE_ID, decoded_from=f"!{outgoing['from']:08x}" if outgoing else "",
            decoded_to=f"!{outgoing['to']:08x}" if outgoing else "", packet_id=sent_id,
            port=port_num, crc="VALID" if outgoing else "", cfo_hz=outgoing.get("cfo_hz", ""),
            signal_dbfs=capture.signal_dbfs, noise_dbfs=capture.noise_dbfs,
            clipping_pct=f"{capture.clipping_pct:.5f}",
            transfer_seconds=f"{capture.transfer_seconds:.3f}", capture=str(capture.path), result=result,
        )
        print(f"{phase}/{case}: {result}")

    def run_burst(self, phase: str, frequency: int, run_tag: str):
        self.drain_pending(phase, "immediate_burst_preclear")
        texts = [f"ORC-{phase}-{run_tag}-BURST-{number}" for number in (1, 2)]
        sent = []
        for text in texts:
            packet = self.mesh.sendText(
                text, destinationId=REMOTE_ID, wantAck=True, channelIndex=0
            )
            sent.append(packet_id(packet))
            self.evidence.event(
                "mesh_tx", phase=phase, case="immediate_burst", packet_id=sent[-1],
                destination=REMOTE_ID, port="TEXT_MESSAGE_APP",
                payload_length=len(text), token=text, tx_power=self.current_power,
            )
        capture = self.wait_capture(phase, "immediate_burst")
        for index, text in enumerate(texts):
            match = next(
                (item for item in capture.decoded if "error" not in item
                 and item.get("from") == LOCAL_NUM and item.get("to") == REMOTE_NUM
                 and item.get("port") == 1 and item.get("payload") == text.encode()), {}
            )
            result = "PASS" if match else "FAIL"
            if capture.clipping_pct > 0.1:
                result = "OVERLOAD" if match else "OVERLOAD_NO_DECODE"
            self.evidence.row(
                timestamp=utc_now(), phase=phase, case=f"burst_{index + 1}",
                frequency_hz=frequency, tx_power=self.current_power,
                payload_length=len(text), expected_from=LOCAL_ID, expected_to=REMOTE_ID,
                decoded_from=f"!{match['from']:08x}" if match else "",
                decoded_to=f"!{match['to']:08x}" if match else "", packet_id=sent[index],
                port="TEXT_MESSAGE_APP", crc="VALID" if match else "",
                cfo_hz=match.get("cfo_hz", ""), signal_dbfs=capture.signal_dbfs,
                noise_dbfs=capture.noise_dbfs, clipping_pct=f"{capture.clipping_pct:.5f}",
                transfer_seconds=f"{capture.transfer_seconds:.3f}",
                capture=str(capture.path), result=result,
            )
            print(f"{phase}/burst_{index + 1}: {result}")

    def run_manual(self, phase: str, frequency: int):
        token = f"ORC-{phase.upper()}-MAN-{uuid.uuid4().hex[:6]}"
        self.drain_pending(phase, "manual_kushcore_reply_preclear")
        input(f"Send exactly '{token}' from Kushcore {REMOTE_ID}, then press Enter here... ")
        capture = self.wait_capture(phase, "manual_kushcore_reply", timeout=60)
        match = next(
            (item for item in capture.decoded if "error" not in item
             and item.get("from") == REMOTE_NUM and item.get("to") == LOCAL_NUM
             and item.get("payload") == token.encode()), {}
        )
        result = "PASS" if match else "FAIL"
        self.evidence.row(
            timestamp=utc_now(), phase=phase, case="manual_kushcore_reply",
            frequency_hz=frequency, tx_power="remote", payload_length=len(token),
            expected_from=REMOTE_ID, expected_to=LOCAL_ID,
            decoded_from=f"!{match['from']:08x}" if match else "",
            decoded_to=f"!{match['to']:08x}" if match else "", packet_id=match.get("id", ""),
            port="TEXT_MESSAGE_APP", crc="VALID" if match else "", cfo_hz=match.get("cfo_hz", ""),
            signal_dbfs=capture.signal_dbfs, noise_dbfs=capture.noise_dbfs,
            clipping_pct=f"{capture.clipping_pct:.5f}",
            transfer_seconds=f"{capture.transfer_seconds:.3f}", capture=str(capture.path), result=result,
        )
        print(f"{phase}/manual_kushcore_reply: {result}")

    def run_phase(self, phase: str, frequency: int, smoke: bool):
        run_tag = uuid.uuid4().hex[:6]
        cases = []
        for power in (2, 15, 30):
            prefix = f"ORC-{phase}-{run_tag}-P{power}-"
            cases.extend([
                (power, f"p{power}_short_broadcast", prefix + "SHORT", "^all", False),
                (power, f"p{power}_directed_64", self.exact_text(prefix + "D64-", 64), REMOTE_ID, True),
                (power, f"p{power}_directed_180", self.exact_text(prefix + "D180-", 180), REMOTE_ID, True),
            ])
        if smoke:
            cases = cases[:3]
        case_results = []
        for power, case, text, destination, want_ack in cases:
            if power != self.current_power:
                self.set_lora(power=power)
            case_results.append(
                self.run_text(phase, case, frequency, text, destination, want_ack)
            )
        if smoke:
            if not all(case_results):
                raise RuntimeError("three-case smoke gate failed")
            return

        self.set_lora(power=15)
        sequence_results = []
        for number in range(1, 4):
            sequence_results.append(self.run_text(
                phase, f"sequence_{number}", frequency,
                f"ORC-{phase}-{run_tag}-SEQ-{number}", REMOTE_ID, True,
            ))
        if phase == "isolated" and not all(sequence_results):
            raise RuntimeError("isolated three-message consecutive decode gate failed")

        from meshtastic.protobuf import mesh_pb2, telemetry_pb2
        from meshtastic.protobuf.portnums_pb2 import PortNum
        self.run_request(phase, "telemetry_request", frequency, PortNum.TELEMETRY_APP,
                         telemetry_pb2.Telemetry())
        self.run_request(phase, "position_request", frequency, PortNum.POSITION_APP,
                         mesh_pb2.Position())
        self.run_request(phase, "traceroute_request", frequency, PortNum.TRACEROUTE_APP,
                         mesh_pb2.RouteDiscovery())

        self.run_burst(phase, frequency, run_tag)
        self.run_manual(phase, frequency)

    def restore(self):
        if self.restored:
            return
        if self.mesh is not None and self.original is not None:
            try:
                self.set_lora(slot=0, power=30, override=0.0,
                              ignore_mqtt=self.original["ignore_mqtt"])
                mqtt = self.mesh.localNode.moduleConfig.mqtt
                mqtt.enabled = self.original["mqtt_enabled"]
                mqtt.proxy_to_client_enabled = self.original["mqtt_proxy"]
                self.mesh.localNode.writeConfig("mqtt")
                self.evidence.event("hardcore_restored", slot=0, power=30, override_frequency=0)
            except Exception as error:
                self.evidence.event("restore_failed", target="hardcore", error=str(error))
                print(f"WARNING: Hardcore restoration failed: {error}", file=sys.stderr)
        if self.rtl is not None:
            try:
                self.tune_rtl(PUBLIC_HZ)
            except Exception as error:
                self.evidence.event("restore_failed", target="rtl", error=str(error))
                print(f"WARNING: OrcSDR retune failed: {error}", file=sys.stderr)
        self.restored = True

    def close(self):
        if self.mesh is not None:
            self.mesh.close()
        if self.rtl is not None:
            self.rtl.close()
        self.primary_psk = None
        self.local_private_key = None
        self.remote_public_key = None
        self.evidence.close()


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rtl_port", nargs="?", default="COM17")
    parser.add_argument("mesh_port", nargs="?", default="COM24")
    parser.add_argument("--smoke", action="store_true", help="run only three public test cases")
    parser.add_argument("--ambient-seconds", type=int, default=60)
    parser.add_argument("--public-only", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = Path(__file__).resolve().parents[1]
    run_dir = root / ".local" / "lora-test-runs" / datetime.now().strftime("%Y%m%d-%H%M%S")
    suite = RfSuite(args.rtl_port, args.mesh_port, run_dir)
    kushcore_needs_restore = False
    print(f"Evidence directory: {run_dir}")
    try:
        suite.connect()
        suite.ambient(max(0, args.ambient_seconds))
        suite.set_lora(slot=0, power=2, override=0.0, ignore_mqtt=True)
        suite.set_mqtt_disabled()
        input(
            "On Kushcore, select US/LongFast automatic slot 0 and disable MQTT (or ignore MQTT), "
            "then press Enter... "
        )
        suite.tune_rtl(PUBLIC_HZ)
        if not suite.run_text(
            "preflight", "directed_ack", PUBLIC_HZ,
            f"ORC-PREFLIGHT-{uuid.uuid4().hex[:6]}", REMOTE_ID, True,
        ):
            raise RuntimeError("public LongFast preflight directed ACK/decode failed")
        suite.run_phase("public", PUBLIC_HZ, args.smoke)
        if not args.smoke and not args.public_only:
            kushcore_needs_restore = True
            input(
                "Set Kushcore LongFast frequency slot to 24 in Android (leave name/key unchanged), "
                "then press Enter... "
            )
            suite.set_lora(slot=24, override=0.0)
            suite.tune_rtl(ISOLATED_HZ)
            if not suite.run_text(
                "isolated_preflight", "directed_ack", ISOLATED_HZ,
                f"ORC-ISO-PREFLIGHT-{uuid.uuid4().hex[:6]}", REMOTE_ID, True,
            ):
                raise RuntimeError("isolated-slot preflight directed ACK/decode failed")
            suite.run_phase("isolated", ISOLATED_HZ, False)
        if suite.faults:
            raise RuntimeError("RTL stop/restart/reset evidence detected: " + "; ".join(suite.faults))
        print("RF cases complete; restoring Hardcore and OrcSDR now.")
        suite.restore()
        if not args.smoke and not args.public_only:
            input(
                "Restore Kushcore to automatic LongFast slot 0 and its original MQTT setting, "
                "then press Enter for the final RF ACK... "
            )
            kushcore_needs_restore = False
        if not suite.run_text(
            "restoration", "public_longfast_ack", PUBLIC_HZ,
            f"ORC-RESTORE-{uuid.uuid4().hex[:6]}", REMOTE_ID, True,
            allow_ack_only=True,
        ):
            raise RuntimeError("final restoration ACK/decode failed on 906.875 MHz")
        return 0
    except KeyboardInterrupt:
        print("Interrupted; restoring controlled hardware settings.", file=sys.stderr)
        return 130
    except Exception as error:
        suite.evidence.event("suite_failed", error=str(error))
        print(f"SUITE_FAILED: {error}", file=sys.stderr)
        return 1
    finally:
        suite.restore()
        if kushcore_needs_restore:
            print(
                "ACTION REQUIRED: restore Kushcore to automatic LongFast slot 0 and its "
                "original MQTT setting in Android.", file=sys.stderr,
            )
        suite.close()


if __name__ == "__main__":
    raise SystemExit(main())
