#!/usr/bin/env python3
"""Evaluate each eligible native LoRa alternate independently off-device."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import decode_orciq
import replay_lora_orciq


def _analyze_report(report, decode_candidate, bins):
    alternate_line = next(line for line in report["traces"]
                          if line.startswith("RTL_LORA_NATIVE_ALTERNATES "))
    symbol_line = next(line for line in report["traces"]
                       if line.startswith("RTL_LORA_NATIVE_SYMBOLS "))
    symbols = [int(value) for value in symbol_line.split("values=", 1)[1].split(",")]
    expected_packet_id = report["host_packet_id"]
    candidates = []
    for index, alternate in replay_lora_orciq._parse_symbol_alternates(alternate_line).items():
        if index >= len(symbols) or (alternate["symbol"] + 1) % bins != symbols[index]:
            continue
        candidate = symbols.copy()
        candidate[index] = alternate["symbol"]
        packet_id = decode_candidate(candidate)
        primary_magnitude = alternate.get("primary_magnitude")
        alternate_magnitude = alternate.get("alternate_magnitude")
        difference = (primary_magnitude - alternate_magnitude
                      if primary_magnitude is not None and alternate_magnitude is not None
                      else None)
        candidates.append({
            "symbol_index": index,
            "primary_symbol": symbols[index],
            "alternate_symbol": alternate["symbol"],
            "primary_magnitude": primary_magnitude,
            "alternate_magnitude": alternate_magnitude,
            "magnitude_difference": difference,
            "normalized_margin": (difference / primary_magnitude
                                  if difference is not None and primary_magnitude else None),
            "ratio": alternate["ratio"],
            "crc_ok": packet_id is not None,
            "packet_id": packet_id,
            "identity_match": (expected_packet_id is not None and packet_id is not None and
                               packet_id == expected_packet_id),
        })
    for rank, candidate in enumerate(sorted(candidates,
                                            key=lambda item: (item["ratio"], item["symbol_index"])), 1):
        candidate["ratio_rank"] = rank
    ranked_margins = [item for item in candidates if item["magnitude_difference"] is not None]
    for rank, candidate in enumerate(sorted(
            ranked_margins,
            key=lambda item: (item["magnitude_difference"], item["symbol_index"])), 1):
        candidate["margin_rank"] = rank
    winners = [candidate for candidate in candidates if candidate["identity_match"]]
    return {
        "expected_packet_id": expected_packet_id,
        "eligible_candidates": len(candidates),
        "successful_candidates": len(winners),
        "winner": winners[0] if len(winners) == 1 else None,
        "candidates": candidates,
    }


def _decoder(frequency_hz, spreading_factor, bandwidth_hz, sample_rate):
    np, _, _, _, LoRaReceiver, _ = decode_orciq._dependencies()

    def decode(symbols):
        receiver = LoRaReceiver(frequency_hz, spreading_factor, bandwidth_hz, sample_rate,
                                has_header=True, preamble_len=16)
        try:
            data, calculated_crc = receiver.decode(np.asarray(symbols))
        except Exception:
            return None
        packet = bytes(data)
        if calculated_crc is not None and packet[-2:] != bytes(calculated_crc):
            return None
        return int.from_bytes(packet[8:12], "little") if len(packet) >= 12 else None

    return decode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reports", nargs="+", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--frequency", type=int, default=906875000)
    parser.add_argument("--sf", type=int, default=11)
    parser.add_argument("--bandwidth", type=int, default=250000)
    parser.add_argument("--rate", type=int, default=960000)
    args = parser.parse_args()
    decoder = _decoder(args.frequency, args.sf, args.bandwidth, args.rate)
    results = []
    for path in args.reports:
        report = json.loads(path.read_text(encoding="utf-8"))
        result = _analyze_report(report, decoder, 1 << args.sf)
        result["report"] = str(path)
        results.append(result)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_suffix(args.output.suffix + ".tmp")
    temporary.write_text(json.dumps({"results": results}, indent=2) + "\n", encoding="utf-8")
    temporary.replace(args.output)


if __name__ == "__main__":
    main()
