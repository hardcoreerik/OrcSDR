#!/usr/bin/env python3
"""Upload an ORCIQ capture to Tab5 PSRAM and run the native LoRa decoder."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import time
from collections import Counter
from pathlib import Path

import decode_orciq


def _wait_line(connection, prefixes, timeout=15, observed=None):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = connection.readline().decode("utf-8", "replace").strip()
        if any(line.startswith(prefix) for prefix in prefixes):
            return line
        if line and observed is not None:
            observed.append(line)
    raise TimeoutError(f"Timed out waiting for {prefixes}")


def _parse_fields(line):
    fields = {}
    for key, value in re.findall(r"\b([a-z_]+)=([^\s]+)", line):
        try:
            value = int(value)
        except ValueError:
            pass
        fields[key] = value
    return fields


def _parse_records(lines, prefix):
    return [_parse_fields(line) for line in lines if line.startswith(prefix)]


def _upload_iq(connection, iq, *, rate, frequency_hz, sf, bandwidth_hz):
    digest = hashlib.sha256(iq).hexdigest()
    connection.write(
        f"RTL_LORA_REPLAY_BEGIN {len(iq)} {digest} {rate} {frequency_hz} {sf} "
        f"{bandwidth_hz}\n".encode("ascii")
    )
    ready = _wait_line(connection, ("RTL_LORA_REPLAY_READY", "RTL_LORA_REPLAY_ERROR"))
    if ready.startswith("RTL_LORA_REPLAY_ERROR"):
        raise RuntimeError(ready)
    chunk_match = re.search(r"\bchunk=(\d+)", ready)
    chunk = int(chunk_match.group(1)) if chunk_match else 0
    if chunk <= 0:
        raise RuntimeError(f"invalid replay chunk: {ready}")
    sent = 0
    while sent < len(iq):
        data = iq[sent : sent + chunk]
        connection.write(f"RTL_LORA_REPLAY_CHUNK {len(data)}\n".encode("ascii"))
        state = _wait_line(connection, ("RTL_LORA_REPLAY_DATA", "RTL_LORA_REPLAY_ERROR"))
        if state.startswith("RTL_LORA_REPLAY_ERROR"):
            raise RuntimeError(state)
        connection.write(data)
        connection.flush()
        sent += len(data)
        state = _wait_line(
            connection,
            ("RTL_LORA_REPLAY_ACK", "RTL_LORA_REPLAY_QUEUED", "RTL_LORA_REPLAY_ERROR"),
        )
        if state.startswith("RTL_LORA_REPLAY_ERROR"):
            raise RuntimeError(state)
        if sent < len(iq) and state != f"RTL_LORA_REPLAY_ACK bytes={sent}":
            raise RuntimeError(state)
    if state != f"RTL_LORA_REPLAY_QUEUED bytes={len(iq)}":
        raise RuntimeError(state)


def _read_traces(connection, done):
    if re.search(r"\bpreambles=0\b", done):
        return []
    traces = []
    while True:
        line = _wait_line(connection,
                          ("RTL_LORA_NATIVE_TRACE", "RTL_LORA_NATIVE_PREPROCESS",
                           "RTL_LORA_NATIVE_FFT", "RTL_LORA_NATIVE_ALTERNATES",
                           "RTL_LORA_NATIVE_SYMBOLS"), 15)
        traces.append(line)
        if line.startswith("RTL_LORA_NATIVE_SYMBOLS"):
            return traces


def _symbol_difference(reference, native):
    compared = min(len(reference), len(native))
    differences = [native[i] - reference[i] for i in range(compared)]
    indices = [i for i, difference in enumerate(differences) if difference]
    indices.extend(range(compared, max(len(reference), len(native))))
    histogram = Counter(differences)
    result = {
        "first": indices[0] if indices else None,
        "indices": indices,
        "different": len(indices),
        "largest": max(differences, key=abs, default=0),
        "histogram": {
            (f"{difference:+d}" if difference else "0"): count
            for difference, count in sorted(histogram.items())
        },
    }
    if len(reference) != len(native):
        result["length_mismatch"] = {"reference": len(reference), "native": len(native)}
    return result


def _parse_symbol_alternates(line):
    if not line or "values=" not in line:
        return {}
    alternates = {}
    for value in line.split("values=", 1)[1].split(","):
        if not value:
            continue
        fields = value.split(":")
        alternate = {"symbol": int(fields[1]), "ratio": float(fields[2])}
        if len(fields) == 5:
            alternate.update(primary_magnitude=float(fields[3]),
                             alternate_magnitude=float(fields[4]))
        alternates[int(fields[0])] = alternate
    return alternates


def _alternate_coverage(reference, native, alternates):
    compared = min(len(reference), len(native))
    errors = [i for i in range(compared) if reference[i] != native[i]]
    errors.extend(range(compared, max(len(reference), len(native))))
    covered = [i for i in errors
               if i < len(reference) and i in alternates and
               alternates[i]["symbol"] == reference[i]]
    result = {"error_indices": errors, "covered_indices": covered,
              "covered": len(covered), "errors": len(errors)}
    if len(reference) != len(native):
        result["length_mismatch"] = {"reference": len(reference), "native": len(native)}
    return result


def _host_reference(path, raw_override=None):
    np, _, _, _, LoRaReceiver, _ = decode_orciq._dependencies()
    from lora_phy.errors import NoPreambleError

    rate, frequency_hz, sf, bandwidth_hz, raw = decode_orciq.read_capture(path)
    if raw_override is not None:
        raw = raw_override
    iq = np.frombuffer(raw, dtype=np.uint8).astype(np.float32).reshape(-1, 2) - 127.5
    signal = (iq[:, 0] + 1j * iq[:, 1]) / 127.5

    def valid_packet(candidate):
        receiver = LoRaReceiver(frequency_hz, sf, bandwidth_hz, rate,
                                has_header=True, preamble_len=16)
        groups, cfos, _ = receiver.demodulate(candidate)
        for symbols in groups:
            data, calculated_crc = receiver.decode(symbols)
            if not calculated_crc or bytes(data[-2:]) == bytes(calculated_crc):
                packet_id = int.from_bytes(bytes(data[8:12]), "little") if len(data) >= 12 else None
                return {"symbols": symbols.tolist(), "packet_id": packet_id}, cfos
        return None, cfos

    try:
        packet, cfos = valid_packet(signal)
        if packet is not None:
            return packet
        samples = np.arange(len(signal), dtype=np.float64)
        for correction_hz in sorted({float(np.median(cfos)), *map(float, cfos)}, key=abs)[:8]:
            if abs(correction_hz) < 0.5:
                continue
            corrected = signal * np.exp(samples * (-2j * np.pi * correction_hz / rate))
            packet, _ = valid_packet(corrected)
            if packet is not None:
                return packet
    except NoPreambleError:
        pass
    return None


def _impair_iq(iq, snr_db, seed, target_rms):
    np, *_ = decode_orciq._dependencies()
    from lora_lab.candidate_detector import impair_awgn

    values = np.frombuffer(iq, dtype=np.uint8).reshape(-1, 2).astype(np.float32)
    signal = ((values[:, 0] - 127.5) + 1j * (values[:, 1] - 127.5)) / 127.5
    blocks = signal[:signal.size // 4096 * 4096].reshape(-1, 4096)
    active_rms = float(np.percentile(np.sqrt(np.mean(np.abs(blocks) ** 2, axis=1)), 95))
    impaired, details = impair_awgn(signal, active_rms, snr_db, seed, target_rms)
    encoded = np.empty((impaired.size, 2), dtype=np.uint8)
    encoded[:, 0] = np.clip(np.rint(impaired.real * 127.5 + 127.5), 0, 255)
    encoded[:, 1] = np.clip(np.rint(impaired.imag * 127.5 + 127.5), 0, 255)
    return encoded.tobytes(), {"snr_db": snr_db, "seed": seed,
                               "target_rms": target_rms,
                               "measured_snr_db": float(details["measured_snr_db"])}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--port", default="COM17")
    parser.add_argument("--pairing-key", type=Path,
                        default=Path(__file__).resolve().parents[1] / ".orclink" / "ui-doc.key")
    parser.add_argument("--compare-host", action="store_true")
    parser.add_argument("--report", type=Path)
    parser.add_argument("--awgn-snr", type=float)
    parser.add_argument("--seed", type=int, default=90210)
    parser.add_argument("--target-rms", type=float, default=0.05)
    args = parser.parse_args()
    rate, frequency_hz, sf, bandwidth_hz, iq = decode_orciq.read_capture(args.capture)
    impairment = None
    if args.awgn_snr is not None:
        iq, impairment = _impair_iq(iq, args.awgn_snr, args.seed, args.target_rms)

    from help_media import Tab5
    tab5 = Tab5(args.port, args.pairing_key)
    try:
        tab5.authenticate()
        tab5.send("RTL_STOP")
        stopped = tab5.wait(("RTL_STOP_RESULT", "RTL_STOP_ERROR"), 20)
        if stopped != "RTL_STOP_RESULT ESP_OK":
            raise RuntimeError(stopped)
        _upload_iq(tab5.serial, iq, rate=rate, frequency_hz=frequency_hz, sf=sf,
                   bandwidth_hz=bandwidth_hz)
        observed = []
        done = _wait_line(tab5.serial, ("RTL_LORA_NATIVE_DONE",), 180, observed)
        profile = _wait_line(tab5.serial, ("RTL_LORA_NATIVE_PROFILE",), 15, observed)
        traces = _read_traces(tab5.serial, done)
        print(done)
        print(profile)
        for trace in traces:
            print(trace.encode("ascii", "backslashreplace").decode("ascii"))
        report = {"capture": str(args.capture), "impairment": impairment,
                  "done": done, "profile": profile,
                  "traces": traces,
                  "memory": _parse_records(observed, "RTL_LORA_MEMORY "),
                  "packets": _parse_records(observed, "RTL_LORA_NATIVE_PACKET ")}
        if args.compare_host:
            reference = _host_reference(args.capture, iq)
            reference_symbols = reference["symbols"] if reference is not None else None
            report["host_decode"] = reference is not None
            report["host_packet_id"] = reference["packet_id"] if reference is not None else None
            symbol_line = next((line for line in traces
                                if line.startswith("RTL_LORA_NATIVE_SYMBOLS")), None)
            native = ([int(value) for value in symbol_line.split("values=", 1)[1].split(",")]
                      if symbol_line else None)
            comparison = (_symbol_difference(reference_symbols, native)
                          if reference_symbols is not None and native is not None else None)
            report["symbol_difference"] = comparison
            print("RTL_LORA_SYMBOL_DIFF " + json.dumps(comparison, sort_keys=True))
            alternate_line = next((line for line in traces
                                   if line.startswith("RTL_LORA_NATIVE_ALTERNATES")), None)
            alternates = _parse_symbol_alternates(alternate_line)
            coverage = (_alternate_coverage(reference_symbols, native, alternates)
                        if reference_symbols is not None and native is not None else None)
            report["alternate_coverage"] = coverage
            print("RTL_LORA_ALTERNATE_COVERAGE " + json.dumps(coverage, sort_keys=True))
        if args.report:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    finally:
        tab5.close()


if __name__ == "__main__":
    main()
