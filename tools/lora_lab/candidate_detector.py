#!/usr/bin/env python3
"""Measure power, channel occupancy, and repeated LoRa chirps in ORCIQ files."""

from __future__ import annotations

import argparse
import json
import sys
import tempfile
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parents[1]))
from decode_orciq import HEADER, decode_capture, read_capture


def impair_awgn(
    signal: np.ndarray,
    active_rms: float,
    snr_db: float,
    seed: int,
    target_active_rms: float = 0.05,
) -> tuple[np.ndarray, dict]:
    if active_rms <= 0:
        raise ValueError("active_rms must be positive")
    scale = target_active_rms / active_rms
    noise_rms = target_active_rms / np.sqrt(10 ** (snr_db / 10))
    rng = np.random.default_rng(seed)
    noise = rng.normal(0, noise_rms / np.sqrt(2), signal.size) + 1j * rng.normal(
        0, noise_rms / np.sqrt(2), signal.size
    )
    measured_noise_rms = float(np.sqrt(np.mean(np.abs(noise) ** 2)))
    return signal * scale + noise, {
        "scale": scale,
        "noise_rms": noise_rms,
        "measured_snr_db": 20 * np.log10(target_active_rms / measured_noise_rms),
    }


def downchirp(sf: int, bandwidth: int, rate: int) -> np.ndarray:
    samples = round(rate * (1 << sf) / bandwidth)
    time = np.arange(samples) / rate
    slope = -(bandwidth * bandwidth) / (1 << sf)
    phase = 2 * np.pi * (bandwidth * 0.5 + 0.5 * slope * time) * time
    return np.exp(1j * phase)


def chirp_metrics(signal: np.ndarray, sf: int, bandwidth: int, rate: int) -> dict:
    reference = downchirp(sf, bandwidth, rate)
    samples = reference.size
    fft_size = samples * 4
    previous = None
    consecutive = 0
    max_consecutive = 0
    peak_to_median = 0.0
    for start in range(0, signal.size - samples + 1, samples):
        spectrum = np.fft.fft(signal[start : start + samples] * reference, fft_size)
        half = fft_size // 2
        magnitude = np.abs(spectrum[:half]) + np.abs(spectrum[half:])
        peak = int(np.argmax(magnitude))
        peak_to_median = max(
            peak_to_median,
            float(magnitude[peak] / max(np.median(magnitude), 1e-12)),
        )
        if previous is None:
            consecutive = 1
        else:
            delta = abs(peak - previous)
            consecutive = consecutive + 1 if min(delta, half - delta) <= 4 else 1
        previous = peak
        max_consecutive = max(max_consecutive, consecutive)
    return {
        "max_consecutive": max_consecutive,
        "peak_to_median": round(peak_to_median, 3),
    }


def _resample(signal: np.ndarray, source_rate: int, target_rate: int) -> np.ndarray:
    output_size = signal.size * target_rate // source_rate
    position = np.arange(output_size, dtype=np.float64) * source_rate / target_rate
    first = position.astype(np.int64)
    fraction = position - first
    second = np.minimum(first + 1, signal.size - 1)
    return signal[first] * (1 - fraction) + signal[second] * fraction


def capture_metrics(path: Path) -> dict:
    rate, frequency, sf, bandwidth, raw = read_capture(path)
    values = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 2).astype(np.float32)
    signal = ((values[:, 0] - 127.5) + 1j * (values[:, 1] - 127.5)) / 127.5
    block_size = 4096
    blocks = signal[: signal.size // block_size * block_size].reshape(-1, block_size)
    rms = np.sqrt(np.mean(np.abs(blocks) ** 2, axis=1))
    spectrum = np.fft.fftshift(np.fft.fft(blocks, axis=1), axes=1)
    frequencies = np.fft.fftshift(np.fft.fftfreq(block_size, 1 / rate))
    power = np.abs(spectrum) ** 2
    channel_ratio = power[:, np.abs(frequencies) <= bandwidth / 2].sum(axis=1) / np.maximum(
        power.sum(axis=1), 1e-20
    )
    decode_rate = bandwidth * 2
    chirps = chirp_metrics(_resample(signal, rate, decode_rate), sf, bandwidth, decode_rate)
    return {
        "path": str(path),
        "sample_rate": rate,
        "frequency_hz": frequency,
        "sf": sf,
        "bandwidth_hz": bandwidth,
        "raw_bytes": len(raw),
        "power_rms_p95": round(float(np.percentile(rms, 95)), 6),
        "power_rms_max": round(float(rms.max()), 6),
        "channel_ratio_p95": round(float(np.percentile(channel_ratio, 95)), 6),
        "channel_ratio_max": round(float(channel_ratio.max()), 6),
        **chirps,
    }


def impairment_metrics(
    path: Path, snr_db: float, seed: int, target_active_rms: float = 0.05
) -> dict:
    rate, _, sf, bandwidth, raw = read_capture(path)
    values = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 2).astype(np.float32)
    signal = ((values[:, 0] - 127.5) + 1j * (values[:, 1] - 127.5)) / 127.5
    blocks = signal[: signal.size // 4096 * 4096].reshape(-1, 4096)
    active_rms = float(np.percentile(np.sqrt(np.mean(np.abs(blocks) ** 2, axis=1)), 95))
    impaired, details = impair_awgn(
        signal, active_rms, snr_db, seed, target_active_rms
    )
    clipped = (np.abs(impaired.real) > 1) | (np.abs(impaired.imag) > 1)
    encoded = np.empty((impaired.size, 2), dtype=np.uint8)
    encoded[:, 0] = np.clip(np.rint(impaired.real * 127.5 + 127.5), 0, 255)
    encoded[:, 1] = np.clip(np.rint(impaired.imag * 127.5 + 127.5), 0, 255)
    quantized = ((encoded[:, 0].astype(np.float32) - 127.5) + 1j * (
        encoded[:, 1].astype(np.float32) - 127.5
    )) / 127.5

    with tempfile.TemporaryDirectory() as directory:
        temporary = Path(directory) / path.name
        with path.open("rb") as source:
            temporary.write_bytes(source.read(HEADER.size) + encoded.tobytes())
        started = time.perf_counter()
        try:
            decoded = decode_capture(temporary)
            decode_error = None
        except ValueError as error:
            decoded = []
            decode_error = str(error)
        decode_seconds = time.perf_counter() - started

    return {
        "snr_db": snr_db,
        "seed": seed,
        "active_rms_p95": round(active_rms, 6),
        "target_active_rms": target_active_rms,
        "signal_scale": details["scale"],
        "noise_rms": details["noise_rms"],
        "measured_snr_db": round(float(details["measured_snr_db"]), 3),
        "clipped_sample_percent": round(float(np.mean(clipped) * 100), 6),
        **chirp_metrics(_resample(quantized, rate, bandwidth * 2), sf, bandwidth, bandwidth * 2),
        "host_valid_packets": sum("error" not in result for result in decoded),
        "host_valid_packet_ids": [
            {key: result.get(key) for key in ("from", "id", "port")}
            for result in decoded
            if "error" not in result
        ],
        "host_decode_errors": [result["error"] for result in decoded if "error" in result],
        "host_decode_exception": decode_error,
        "host_decode_seconds": round(decode_seconds, 3),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("captures", nargs="+", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument(
        "--snr-db",
        help="comma-separated deterministic AWGN levels; runs the full host decoder",
    )
    parser.add_argument("--seed", type=int, default=90210)
    parser.add_argument("--target-rms", type=float, default=0.05)
    args = parser.parse_args()
    if args.target_rms <= 0:
        parser.error("--target-rms must be positive")
    captures = [capture_metrics(path) for path in args.captures]
    report = {"captures": captures}
    if args.snr_db:
        levels = [float(value) for value in args.snr_db.split(",")]
        report["impairments"] = [
            {
                "path": str(path),
                "levels": [
                    impairment_metrics(
                        path,
                        level,
                        args.seed + capture_index * len(levels) + level_index,
                        args.target_rms,
                    )
                    for level_index, level in enumerate(levels)
                ],
            }
            for capture_index, path in enumerate(args.captures)
        ]
    text = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
    print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
