#!/usr/bin/env python3
"""Decode OrcSDR CU8 captures into LoRa and Meshtastic packets."""

from __future__ import annotations

import argparse
import base64
import hashlib
import re
import struct
import sys
import tempfile
import time
import types
from pathlib import Path

MAGIC = b"ORCIQ01\0"
HEADER = struct.Struct("<8sIIIIHBBII")
MESH_HEADER = struct.Struct("<III4B")
DEFAULT_PSK = bytes.fromhex("d4f1bb3a20290759f0bcffabcf4e6901")
PORT_NAMES = {
    0: "UNKNOWN_APP", 1: "TEXT_MESSAGE_APP", 2: "REMOTE_HARDWARE_APP",
    3: "POSITION_APP", 4: "NODEINFO_APP", 5: "ROUTING_APP", 6: "ADMIN_APP",
    7: "TEXT_MESSAGE_COMPRESSED_APP", 8: "WAYPOINT_APP", 9: "AUDIO_APP",
    10: "DETECTION_SENSOR_APP", 11: "ALERT_APP", 12: "KEY_VERIFICATION_APP",
    13: "REMOTE_SHELL_APP", 32: "REPLY_APP", 33: "IP_TUNNEL_APP",
    34: "PAXCOUNTER_APP", 35: "STORE_FORWARD_PLUSPLUS_APP",
    36: "NODE_STATUS_APP", 37: "MESH_BEACON_APP", 64: "SERIAL_APP",
    65: "STORE_FORWARD_APP", 66: "RANGE_TEST_APP", 67: "TELEMETRY_APP",
    68: "ZPS_APP", 69: "SIMULATOR_APP", 70: "TRACEROUTE_APP",
    71: "NEIGHBORINFO_APP", 72: "ATAK_PLUGIN", 73: "MAP_REPORT_APP",
    74: "POWERSTRESS_APP", 75: "LORAWAN_BRIDGE", 76: "RETICULUM_TUNNEL_APP",
    77: "CAYENNE_APP", 78: "ATAK_PLUGIN_V2", 79: "LORA_OTA_APP",
    112: "GROUPALARM_APP", 256: "PRIVATE_APP", 257: "ATAK_FORWARDER",
}
TEXT_PORTS = {1, 10, 11, 13, 32, 66}
IQ_DONE_RE = re.compile(r"^RTL_IQ_DONE\b")


def _dependencies():
    try:
        import numpy as np
        from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
        try:
            from lora_phy import LoRaReceiver, LoRaTransmitter
        except ModuleNotFoundError as error:
            if error.name != "matplotlib":
                raise
            # lora-phy 0.2 imports its optional plotting module unconditionally.
            matplotlib = types.ModuleType("matplotlib")
            matplotlib.__path__ = []
            sys.modules["matplotlib"] = matplotlib
            sys.modules["matplotlib.pyplot"] = types.ModuleType("matplotlib.pyplot")
            from lora_phy import LoRaReceiver, LoRaTransmitter
    except ImportError as error:
        raise SystemExit(
            "Missing decoder dependency. Run: "
            f"{sys.executable} -m pip install -r tools/requirements-lora.txt"
        ) from error
    return np, Cipher, algorithms, modes, LoRaReceiver, LoRaTransmitter


def read_capture(path: Path):
    raw = path.read_bytes()
    if len(raw) < HEADER.size:
        raise ValueError("capture is shorter than the 36-byte ORCIQ header")
    magic, header_bytes, rate, freq, data_bytes, fmt, sf, _, bw, flags = HEADER.unpack_from(raw)
    if magic != MAGIC or header_bytes != HEADER.size or fmt != 1 or flags != 0:
        raise ValueError("unsupported ORCIQ header")
    iq = raw[header_bytes:]
    if len(iq) != data_bytes or data_bytes % 2:
        raise ValueError(f"IQ length mismatch: header={data_bytes}, file={len(iq)}")
    return rate, freq, sf, bw, iq


def _varint(data: bytes, offset: int) -> tuple[int, int]:
    value = 0
    for shift in range(0, 70, 7):
        if offset >= len(data):
            raise ValueError("truncated protobuf varint")
        byte = data[offset]
        offset += 1
        value |= (byte & 0x7F) << shift
        if not byte & 0x80:
            return value, offset
    raise ValueError("protobuf varint is too long")


def _parse_data_message(data: bytes) -> dict:
    port = None
    payload = None
    request_id = 0
    offset = 0
    while offset < len(data):
        tag, offset = _varint(data, offset)
        field, wire = tag >> 3, tag & 7
        if wire == 0:
            value, offset = _varint(data, offset)
        elif wire == 1:
            if offset + 8 > len(data):
                raise ValueError("truncated fixed64")
            value, offset = data[offset:offset + 8], offset + 8
        elif wire == 2:
            size, offset = _varint(data, offset)
            if offset + size > len(data):
                raise ValueError("truncated length-delimited field")
            value, offset = data[offset:offset + size], offset + size
        elif wire == 5:
            if offset + 4 > len(data):
                raise ValueError("truncated fixed32")
            value, offset = data[offset:offset + 4], offset + 4
        else:
            raise ValueError(f"unsupported protobuf wire type {wire}")
        if field == 1 and wire == 0:
            port = int(value)
        elif field == 2 and wire == 2:
            payload = bytes(value)
        elif field == 6 and wire in (0, 5):
            request_id = int(value) if wire == 0 else int.from_bytes(value, "little")
    if port is None or payload is None or not 0 <= port <= 511:
        raise ValueError("not a plausible Meshtastic Data protobuf")
    return {"port": port, "payload": payload, "request_id": request_id}


def parse_data_protobuf(data: bytes) -> tuple[int, bytes]:
    message = _parse_data_message(data)
    return message["port"], message["payload"]


def _expand_psk(raw: bytes) -> bytes:
    if len(raw) == 0 or raw == b"\0":
        return b""
    if len(raw) == 1:
        if raw == b"\1":
            return DEFAULT_PSK
        key = bytearray(DEFAULT_PSK)
        key[-1] = (key[-1] + raw[0] - 1) & 0xFF
        return bytes(key)
    if len(raw) not in (16, 32):
        raise ValueError("PSK must decode to 0, 1, 16, or 32 bytes")
    return raw


def read_psk(path: Path) -> bytes:
    text = path.read_text(encoding="ascii").strip()
    try:
        raw = bytes.fromhex(text)
    except ValueError:
        try:
            raw = base64.b64decode(text, validate=True)
        except ValueError as error:
            raise ValueError("PSK file must contain hex or base64") from error
    return _expand_psk(raw)


def _crypt(payload: bytes, key: bytes, sender: int, packet_id: int) -> bytes:
    if not key:
        return payload
    _, Cipher, algorithms, modes, _, _ = _dependencies()
    nonce = struct.pack("<QI", packet_id, sender) + b"\0" * 4
    return Cipher(algorithms.AES(key), modes.CTR(nonce)).decryptor().update(payload)


def _decrypt_pki(
    payload: bytes, private_key: bytes, remote_public_key: bytes,
    sender: int, packet_id: int,
) -> bytes:
    if len(payload) <= 12 or len(private_key) != 32 or len(remote_public_key) != 32:
        raise ValueError("invalid PKI payload or key length")
    from cryptography.hazmat.primitives.asymmetric.x25519 import (
        X25519PrivateKey, X25519PublicKey,
    )
    from cryptography.hazmat.primitives.ciphers.aead import AESCCM

    shared = X25519PrivateKey.from_private_bytes(private_key).exchange(
        X25519PublicKey.from_public_bytes(remote_public_key)
    )
    nonce = bytearray(struct.pack("<QI", packet_id, sender) + b"\0" * 4)
    nonce[4:8] = payload[-4:]
    return AESCCM(hashlib.sha256(shared).digest(), tag_length=8).decrypt(
        bytes(nonce[:13]), payload[:-4], None
    )


def decode_mesh_packet(
    frame: bytes,
    keys: list[tuple[str, bytes]],
    pki_keys: list[tuple[str, bytes, bytes]] | None = None,
) -> dict:
    if len(frame) <= MESH_HEADER.size:
        raise ValueError("LoRa payload is shorter than the 16-byte Meshtastic header")
    to_node, from_node, packet_id, flags, channel, next_hop, relay = MESH_HEADER.unpack_from(frame)
    encrypted = frame[MESH_HEADER.size:]
    failures = []
    for key_name, private_key, remote_public_key in pki_keys or []:
        try:
            data = _parse_data_message(
                _decrypt_pki(encrypted, private_key, remote_public_key, from_node, packet_id)
            )
            return {
                "to": to_node, "from": from_node, "id": packet_id,
                "hop_limit": flags & 7, "want_ack": bool(flags & 8),
                "channel_hash": channel, "next_hop": next_hop, "relay": relay,
                "key": key_name, "encryption": "pki", **data,
            }
        except Exception as error:
            failures.append(f"{key_name}: {type(error).__name__}")
    for key_name, key in keys:
        try:
            data = _parse_data_message(_crypt(encrypted, key, from_node, packet_id))
            return {
                "to": to_node, "from": from_node, "id": packet_id,
                "hop_limit": flags & 7, "want_ack": bool(flags & 8),
                "channel_hash": channel, "next_hop": next_hop, "relay": relay,
                "key": key_name, "encryption": "channel", **data,
            }
        except ValueError as error:
            failures.append(f"{key_name}: {error}")
    raise ValueError("decryption/protobuf validation failed (" + "; ".join(failures) + ")")


def _encode_varint(value: int) -> bytes:
    out = bytearray()
    while value > 0x7F:
        out.append((value & 0x7F) | 0x80)
        value >>= 7
    out.append(value)
    return bytes(out)


def _data_message(port: int, payload: bytes) -> bytes:
    return b"\x08" + _encode_varint(port) + b"\x12" + _encode_varint(len(payload)) + payload


def decode_capture(
    path: Path,
    psk_file: Path | None = None,
    channel_psks: list[tuple[str, bytes]] | None = None,
    pki_keys: list[tuple[str, bytes, bytes]] | None = None,
):
    np, _, _, _, LoRaReceiver, _ = _dependencies()
    from lora_phy.errors import NoPreambleError

    rate, freq, sf, bw, raw = read_capture(path)
    iq = (np.frombuffer(raw, dtype=np.uint8).astype(np.float32).reshape(-1, 2) - 127.5)
    signal = (iq[:, 0] + 1j * iq[:, 1]) / 127.5
    keys = [("clear", b""), ("public-default", DEFAULT_PSK)]
    if channel_psks:
        keys[0:0] = [(name, _expand_psk(psk)) for name, psk in channel_psks]
    if psk_file:
        keys.insert(0, ("key-file", read_psk(psk_file)))

    def demodulate(candidate):
        receiver = LoRaReceiver(freq, sf, bw, rate, has_header=True, preamble_len=16)
        symbols, cfos, netids = receiver.demodulate(candidate)
        results = []
        valid_crc = 0
        for index, packet_symbols in enumerate(symbols):
            data, calculated_crc = receiver.decode(packet_symbols)
            packet = bytes(data)
            if calculated_crc is not None:
                received_crc = packet[-2:]
                if received_crc != bytes(calculated_crc):
                    results.append({"error": "LoRa CRC mismatch", "index": index})
                    continue
                packet = packet[:-2]
            valid_crc += 1
            try:
                result = decode_mesh_packet(packet, keys, pki_keys)
                result.update(index=index, cfo_hz=cfos[index], netid=netids[index])
                results.append(result)
            except ValueError as error:
                results.append({"error": str(error), "index": index, "raw": packet.hex()})
        return results, valid_crc, cfos

    try:
        results, _, cfos = demodulate(signal)
        if not cfos or not any("error" in result for result in results):
            return results
        valid = {
            (result.get("from"), result.get("id"), result.get("port")): result
            for result in results if "error" not in result
        }
        corrections = sorted(
            {float(np.median(cfos)), *(float(cfo) for cfo in cfos)}, key=abs
        )
        samples = np.arange(len(signal), dtype=np.float64)
        for correction_hz in corrections[:8]:
            if abs(correction_hz) < 0.5:
                continue
            phase = samples * (-2j * np.pi * correction_hz / rate)
            corrected_results, _, _ = demodulate(signal * np.exp(phase))
            for result in corrected_results:
                if "error" not in result:
                    result["cfo_hz"] += correction_hz
                    valid.setdefault(
                        (result.get("from"), result.get("id"), result.get("port")), result
                    )
        errors = [result for result in results if "error" in result]
        return list(valid.values()) + errors
    except NoPreambleError as error:
        raise ValueError(
            f"no LoRa preamble/sync found (SF{sf}, BW {bw}, {rate} S/s)"
        ) from error


def _message_command(result: dict) -> bytes:
    text = result["payload"].decode("utf-8", errors="replace")
    while len(text.encode("utf-8")) > 95:
        text = text[:-1]
    return (
        f"LORA_MESSAGE {result['from']:08x} {result['id']:08x} "
        f"{text.encode('utf-8').hex()}\n"
    ).encode("ascii")


def print_results(results: list[dict]) -> tuple[bool, list[dict]]:
    found = False
    messages = []
    for result in results:
        if "error" in result:
            print(f"PACKET_{result['index']} REJECTED {result['error']}")
            continue
        found = True
        payload = result["payload"]
        port = result["port"]
        print(
            f"PACKET_{result['index']} from=!{result['from']:08x} to=!{result['to']:08x} "
            f"id={result['id']:08x} hops={result['hop_limit']} cfo={result['cfo_hz']:.1f}Hz "
            f"port={PORT_NAMES.get(port, str(port))} key={result['key']}"
        )
        if port in TEXT_PORTS:
            print("MESSAGE " + payload.decode("utf-8", errors="replace"))
            messages.append(result)
        else:
            print("PAYLOAD_HEX " + payload.hex())
    return found, messages


def _open_serial(port: str):
    try:
        import serial
    except ImportError as error:
        raise RuntimeError(
            f"Missing pyserial. Run: {sys.executable} -m pip install -r "
            "tools/requirements-lora.txt"
        ) from error
    connection = serial.Serial()
    connection.port = port
    connection.baudrate = 115200
    connection.timeout = 0.5
    connection.write_timeout = 5
    connection.dtr = False
    connection.rts = False
    connection.open()
    connection.reset_input_buffer()
    return connection


def _wait_line(connection, prefixes: tuple[str, ...], timeout: float = 20) -> str:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = connection.readline().decode("utf-8", errors="replace").strip()
        if not line:
            continue
        if line.startswith(prefixes):
            return line
    raise TimeoutError("timed out waiting for " + " or ".join(prefixes))


def _read_exact(connection, count: int) -> bytes:
    data = bytearray()
    while len(data) < count:
        chunk = connection.read(count - len(data))
        if not chunk:
            raise TimeoutError("serial IQ transfer ended early")
        data.extend(chunk)
    return bytes(data)


def _download_psram_iq(connection, fields: dict[str, int], batch_size: int) -> bytes:
    connection.write(b"RTL_IQ_GET_BEGIN\n")
    ready = _wait_line(connection, ("RTL_IQ_GET_READY", "RTL_IQ_GET_ERROR"))
    if ready.startswith("RTL_IQ_GET_ERROR"):
        raise RuntimeError(ready)
    chunk_match = re.search(r"chunk=(\d+)", ready)
    if not chunk_match:
        raise RuntimeError("invalid RTL_IQ_GET_READY response: " + ready)
    chunk_bytes = int(chunk_match.group(1))
    expected = fields["bytes"]
    capture = bytearray()
    while len(capture) < expected:
        remaining_chunks = (expected - len(capture) + chunk_bytes - 1) // chunk_bytes
        batch = min(batch_size, remaining_chunks)
        connection.write(b"RTL_IQ_GET_CHUNK\n" * batch)
        for _ in range(batch):
            data_line = _wait_line(connection, ("RTL_IQ_GET_DATA", "RTL_IQ_GET_ERROR"))
            if data_line.startswith("RTL_IQ_GET_ERROR"):
                raise RuntimeError(data_line)
            count_match = re.fullmatch(r"RTL_IQ_GET_DATA bytes=(\d+)", data_line)
            if not count_match:
                raise RuntimeError("invalid RTL_IQ_GET_DATA response: " + data_line)
            capture.extend(_read_exact(connection, int(count_match.group(1))))
    done = _wait_line(connection, ("RTL_IQ_GET_DONE", "RTL_IQ_GET_ERROR"), 30)
    if done.startswith("RTL_IQ_GET_ERROR"):
        raise RuntimeError(done)
    digest = hashlib.sha256(capture).hexdigest()
    sha_match = re.search(r"sha256=([0-9a-f]{64})", done)
    if not sha_match or sha_match.group(1) != digest or len(capture) != expected:
        raise RuntimeError("PSRAM IQ transfer integrity check failed")
    return bytes(capture)


def _abort_iq_transfer(connection) -> None:
    connection.reset_input_buffer()
    connection.write(b"RTL_IQ_GET_ABORT\n")
    response = _wait_line(
        connection, ("RTL_IQ_GET_ABORTED", "RTL_IQ_GET_ERROR"), timeout=10
    )
    if not response.startswith("RTL_IQ_GET_ABORTED") or "ready=true" not in response:
        raise RuntimeError("capture was not retained after transfer abort: " + response)


def _retrieve_latest_iq(connection, retry_callback=None) -> tuple[str, bytes]:
    connection.write(b"RTL_IQ_RETRIEVE_BEGIN\n")
    state = _wait_line(
        connection,
        ("RTL_IQ_RETRIEVE_STOPPING", "RTL_IQ_RETRIEVE_READY", "RTL_IQ_RETRIEVE_ERROR"),
    )
    if state.startswith("RTL_IQ_RETRIEVE_ERROR"):
        raise RuntimeError(state)
    if state.startswith("RTL_IQ_RETRIEVE_STOPPING"):
        _wait_line(connection, ("RTL_STOP bytes=",), 20)

    if "storage=psram" in state:
        fields = {
            key: int(value)
            for key, value in re.findall(
                r"(bytes|rate|frequency_hz|sf|bw)=(\d+)", state
            )
        }
        if set(fields) != {"bytes", "rate", "frequency_hz", "sf", "bw"}:
            raise RuntimeError("invalid PSRAM retrieve response: " + state)
        failures = []
        capture = None
        for attempt, batch_size in enumerate((4, 1, 1), start=1):
            try:
                capture = _download_psram_iq(connection, fields, batch_size)
                break
            except (OSError, RuntimeError, TimeoutError) as error:
                failures.append(f"attempt {attempt}: {error}")
                if retry_callback is not None:
                    retry_callback(attempt, str(error))
                if attempt < 3:
                    _abort_iq_transfer(connection)
        if capture is None:
            raise RuntimeError("PSRAM IQ transfer failed after retries (" + "; ".join(failures) + ")")
        expected = fields["bytes"]
        header = HEADER.pack(
            MAGIC, HEADER.size, fields["rate"], fields["frequency_hz"], expected,
            1, fields["sf"], 0, fields["bw"], 0,
        )
        name = (
            f"iq_serial_{int(time.time() * 1000)}_{fields['frequency_hz']}_"
            f"sf{fields['sf']}_bw{fields['bw']}.orciq"
        )
        return name, header + capture

    match = re.search(r"pathhex=([0-9a-fA-F]+)$", state)
    if not match:
        raise RuntimeError("invalid retrieve response: " + state)
    path_hex = match.group(1).lower()
    path = bytes.fromhex(path_hex).decode("ascii")

    connection.write(f"SD_GET_BEGIN {path_hex}\n".encode("ascii"))
    ready = _wait_line(connection, ("SD_GET_READY", "SD_GET_ERROR"))
    if ready.startswith("SD_GET_ERROR"):
        raise RuntimeError(ready)
    size_match = re.search(r"bytes=(\d+)", ready)
    if not size_match:
        raise RuntimeError("invalid SD_GET_READY response: " + ready)
    expected = int(size_match.group(1))
    capture = bytearray()
    while len(capture) < expected:
        connection.write(b"SD_GET_CHUNK\n")
        data_line = _wait_line(connection, ("SD_GET_DATA", "SD_GET_ERROR"))
        if data_line.startswith("SD_GET_ERROR"):
            raise RuntimeError(data_line)
        count_match = re.fullmatch(r"SD_GET_DATA bytes=(\d+)", data_line)
        if not count_match:
            raise RuntimeError("invalid SD_GET_DATA response: " + data_line)
        capture.extend(_read_exact(connection, int(count_match.group(1))))
    done = _wait_line(connection, ("SD_GET_DONE", "SD_GET_ERROR"), 30)
    if done.startswith("SD_GET_ERROR"):
        raise RuntimeError(done)
    sha_match = re.search(r"sha256=([0-9a-f]{64})", done)
    digest = hashlib.sha256(capture).hexdigest()
    if not sha_match or sha_match.group(1) != digest or len(capture) != expected:
        raise RuntimeError("IQ transfer integrity check failed")
    return path, bytes(capture)


def watch_tab5(port: str, psk_file: Path | None, capture_dir: Path | None) -> int:
    if capture_dir is not None:
        capture_dir.mkdir(parents=True, exist_ok=True)
    connection = _open_serial(port)
    print(f"WATCHING {port}: waiting for adaptive RTL_LORA_ENERGY captures")
    try:
        connection.write(b"RTL_IQ_STATUS\n")
        status = _wait_line(connection, ("RTL_IQ_STATUS",))
        pending = "ready=true" in status
        while True:
            if pending:
                line = "RTL_IQ_DONE pending=psram"
                pending = False
            else:
                line = connection.readline().decode("utf-8", errors="replace").strip()
            if not line:
                continue
            if line.startswith(("RTL_LORA_ENERGY", "RTL_IQ_START", "RTL_IQ_DONE")):
                print(line)
            if not IQ_DONE_RE.match(line):
                continue
            temporary = None
            verified = False
            try:
                remote_path, capture_bytes = _retrieve_latest_iq(connection)
                if capture_dir is None:
                    temporary = tempfile.TemporaryDirectory()
                    local_path = Path(temporary.name) / Path(remote_path).name
                else:
                    temporary = None
                    local_path = capture_dir / Path(remote_path).name
                local_path.write_bytes(capture_bytes)
                verified = True
                print(f"IQ_VERIFIED {local_path} bytes={len(capture_bytes)}")
                results = decode_capture(local_path, psk_file)
                _, messages = print_results(results)
                for result in messages:
                    connection.write(_message_command(result))
                    response = _wait_line(connection, ("LORA_MESSAGE_OK", "LORA_MESSAGE_ERROR"))
                    if response.startswith("LORA_MESSAGE_ERROR"):
                        raise RuntimeError(response)
                    print(response)
            except (OSError, RuntimeError, TimeoutError, ValueError) as error:
                print(f"LIVE_DECODE_FAILED {error}", file=sys.stderr)
            finally:
                if temporary is not None:
                    temporary.cleanup()
                if verified:
                    connection.write(b"RTL_IQ_RETRIEVE_END\n")
                    resumed = _wait_line(
                        connection,
                        ("RTL_IQ_RETRIEVE_RESUMING", "RTL_IQ_RETRIEVE_DONE"),
                    )
                    print(resumed)
                else:
                    print("RTL_IQ_RETAINED retry the pending capture", file=sys.stderr)
    except KeyboardInterrupt:
        print("WATCH_STOPPED")
        return 0
    finally:
        connection.close()


def self_test() -> None:
    np, _, _, _, LoRaReceiver, LoRaTransmitter = _dependencies()
    sender, packet_id = 0xA1B2C3D4, 0x10203040
    plaintext = _data_message(1, b"OrcSDR Meshtastic self-test")
    header = MESH_HEADER.pack(0xFFFFFFFF, sender, packet_id, 3, 8, 0, sender & 0xFF)
    frame = header + _crypt(plaintext, DEFAULT_PSK, sender, packet_id)
    tx = LoRaTransmitter(9, 125000, 250000, coding_rate=1, preamble_len=16)
    signal = tx.modulate(tx.encode(np.frombuffer(frame, dtype=np.uint8)), cfo=650)
    cu8 = np.column_stack((signal.real, signal.imag))
    cu8 = np.clip(np.rint(cu8 * 100 + 127.5), 0, 255).astype(np.uint8).tobytes()
    probe_iq = np.frombuffer(cu8, dtype=np.uint8).astype(np.float32).reshape(-1, 2) - 127.5
    probe_signal = (probe_iq[:, 0] + 1j * probe_iq[:, 1]) / 127.5
    probe = LoRaReceiver(906875000, 9, 125000, 250000, has_header=True, preamble_len=16)
    probe_symbols, _, _ = probe.demodulate(probe_signal)
    probe_data, probe_crc = probe.decode(probe_symbols[0])
    assert probe_crc is not None and bytes(probe_data[-2:]) != bytes(probe_crc)
    orciq = HEADER.pack(MAGIC, HEADER.size, 250000, 906875000, len(cu8), 1, 9, 0,
                        125000, 0) + cu8
    with tempfile.TemporaryDirectory() as directory:
        capture = Path(directory) / "self-test.orciq"
        capture.write_bytes(orciq)
        results = decode_capture(capture, None)
    valid_results = [result for result in results if "error" not in result]
    assert len(valid_results) == 1
    result = valid_results[0]
    assert result["payload"] == b"OrcSDR Meshtastic self-test" and result["port"] == 1
    assert _message_command(result) == (
        b"LORA_MESSAGE a1b2c3d4 10203040 "
        b"4f7263534452204d6573687461737469632073656c662d74657374\n"
    )
    print("SELF_TEST_OK ORCIQ/CU8 + CFO retry + LoRa PHY/CRC + Meshtastic AES-CTR/protobuf + UTF-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", nargs="?", type=Path, help="Tab5 .orciq capture")
    parser.add_argument("--psk-file", type=Path, help="ASCII hex/base64 channel PSK (never printed)")
    parser.add_argument("--watch-port", help="watch a Tab5 serial port and return decoded text to its display")
    parser.add_argument("--capture-dir", type=Path, help="keep verified live captures in this existing directory")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if args.watch_port:
        if args.capture:
            parser.error("capture and --watch-port are mutually exclusive")
        try:
            return watch_tab5(args.watch_port, args.psk_file, args.capture_dir)
        except (OSError, RuntimeError, ValueError) as error:
            print(f"WATCH_FAILED {error}", file=sys.stderr)
            return 2
    if not args.capture:
        parser.error("capture is required unless --self-test or --watch-port is used")
    if args.capture_dir:
        parser.error("--capture-dir is only valid with --watch-port")
    try:
        results = decode_capture(args.capture, args.psk_file)
    except (OSError, ValueError) as error:
        print(f"DECODE_FAILED {error}", file=sys.stderr)
        return 2
    found, _ = print_results(results)
    return 0 if found else 3


if __name__ == "__main__":
    raise SystemExit(main())
