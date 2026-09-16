#!/usr/bin/env python3
"""Analyze raw interleaved CU8 RTL-SDR IQ without changing receiver data."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np


def analyze_capture(
    path: Path,
    rate: int,
    *,
    fft_size: int = 2048,
    dc_guard_fraction: float = 0.01,
    edge_guard_fraction: float = 0.03,
) -> dict[str, object]:
    raw = np.fromfile(path, dtype=np.uint8)
    if raw.size < fft_size * 2 or raw.size % 2:
        raise ValueError("capture must contain an even number of CU8 bytes and one FFT frame")
    pairs = raw.reshape(-1, 2).astype(np.float64) - 127.5
    mean_i, mean_q = np.mean(pairs, axis=0)
    centered = pairs - (mean_i, mean_q)
    i, q = centered[:, 0], centered[:, 1]
    rms_i = float(np.sqrt(np.mean(i * i)))
    rms_q = float(np.sqrt(np.mean(q * q)))
    correlation = float(np.mean(i * q) / max(rms_i * rms_q, np.finfo(float).tiny))

    frame_count = centered.shape[0] // fft_size
    complex_iq = (i + 1j * q)[: frame_count * fft_size].reshape(frame_count, fft_size)
    window = np.hanning(fft_size)
    transformed = np.fft.fftshift(
        np.fft.fft(complex_iq * window, axis=1) / np.sum(window), axes=1
    )
    frame_power = np.abs(transformed) ** 2
    average_power = np.mean(frame_power, axis=0)
    floor = np.finfo(float).tiny
    spectrum_dbfs = 10.0 * np.log10(np.maximum(average_power, floor) / (127.5**2))
    waterfall_dbfs = 10.0 * np.log10(np.maximum(frame_power, floor) / (127.5**2))
    frequencies = np.fft.fftshift(np.fft.fftfreq(fft_size, d=1.0 / rate))

    dc_guard_hz = rate * dc_guard_fraction
    edge_guard_hz = rate * edge_guard_fraction
    negative = (frequencies < -dc_guard_hz) & (frequencies > -rate / 2 + edge_guard_hz)
    positive = (frequencies > dc_guard_hz) & (frequencies < rate / 2 - edge_guard_hz)
    negative_mean = float(np.mean(average_power[negative]))
    positive_mean = float(np.mean(average_power[positive]))

    metrics: dict[str, object] = {
        "capture_bytes": int(raw.size),
        "complex_samples": int(pairs.shape[0]),
        "sample_rate": int(rate),
        "fft_size": int(fft_size),
        "frame_count": int(frame_count),
        "mean_i": float(mean_i),
        "mean_q": float(mean_q),
        "rms_i": rms_i,
        "rms_q": rms_q,
        "i_to_q_rms_delta_db": float(20.0 * np.log10(max(rms_i, floor) / max(rms_q, floor))),
        "i_q_correlation": correlation,
        "clipping_percentage": float(100.0 * np.count_nonzero((raw == 0) | (raw == 255)) / raw.size),
        "negative_half_mean_linear_power": negative_mean,
        "positive_half_mean_linear_power": positive_mean,
        "negative_half_median_db": float(np.median(spectrum_dbfs[negative])),
        "positive_half_median_db": float(np.median(spectrum_dbfs[positive])),
        "half_power_delta_db": float(10.0 * np.log10(max(positive_mean, floor) / max(negative_mean, floor))),
        "frequencies": frequencies,
        "spectrum_dbfs": spectrum_dbfs,
        "waterfall_dbfs": waterfall_dbfs,
    }
    return metrics


def ascii_spectrum(values: np.ndarray, columns: int = 80) -> str:
    blocks = " .:-=+*#"
    chunks = np.array_split(values, columns)
    reduced = np.array([np.max(chunk) for chunk in chunks])
    low, high = np.percentile(reduced, (5, 95))
    scaled = np.clip((reduced - low) / max(high - low, 1e-9), 0.0, 1.0)
    return "".join(blocks[min(int(value * len(blocks)), len(blocks) - 1)] for value in scaled)


def report_text(path: Path, metrics: dict[str, object]) -> str:
    lines = [
        "RTL_IQ_DIAG",
        f"CAPTURE={path}",
        f"RATE={metrics['sample_rate']}",
        f"BYTES={metrics['capture_bytes']}",
        f"FFT={metrics['fft_size']}",
        f"FRAMES={metrics['frame_count']}",
        f"NEG_MEAN_LINEAR={metrics['negative_half_mean_linear_power']:.9g}",
        f"POS_MEAN_LINEAR={metrics['positive_half_mean_linear_power']:.9g}",
        f"NEG_MEDIAN={metrics['negative_half_median_db']:.3f} dBFS",
        f"POS_MEDIAN={metrics['positive_half_median_db']:.3f} dBFS",
        f"HALF_DELTA={metrics['half_power_delta_db']:.3f} dB",
        f"MEAN_I={metrics['mean_i']:.6f}",
        f"MEAN_Q={metrics['mean_q']:.6f}",
        f"I_RMS={metrics['rms_i']:.6f}",
        f"Q_RMS={metrics['rms_q']:.6f}",
        f"I_Q_RMS_DELTA={metrics['i_to_q_rms_delta_db']:.3f} dB",
        f"IQ_CORR={metrics['i_q_correlation']:.6f}",
        f"CLIPPING={metrics['clipping_percentage']:.6f}%",
        "RESULT=UNCLASSIFIED_EMPIRICAL_BASELINE_REQUIRED",
        f"ASCII={ascii_spectrum(metrics['spectrum_dbfs'])}",
    ]
    return "\n".join(lines) + "\n"


def write_artifacts(path: Path, metrics: dict[str, object], write_csv: bool) -> list[Path]:
    frequency_mhz = metrics["frequencies"] / 1e6
    spectrum_path = path.with_name(path.stem + "_spectrum.png")
    waterfall_path = path.with_name(path.stem + "_waterfall.png")
    report_path = path.with_name(path.stem + "_report.txt")

    fig, axis = plt.subplots(figsize=(12, 5))
    axis.plot(frequency_mhz, metrics["spectrum_dbfs"], linewidth=0.8)
    axis.axvline(0, color="red", linewidth=0.8)
    axis.set(xlabel="Baseband offset (MHz)", ylabel="Power (dBFS)", title=path.name)
    axis.grid(alpha=0.25)
    fig.tight_layout()
    fig.savefig(spectrum_path, dpi=150)
    plt.close(fig)

    fig, axis = plt.subplots(figsize=(12, 5))
    image = axis.imshow(
        metrics["waterfall_dbfs"],
        aspect="auto",
        origin="upper",
        extent=(frequency_mhz[0], frequency_mhz[-1], metrics["frame_count"], 0),
        cmap="turbo",
    )
    axis.axvline(0, color="white", linewidth=0.8)
    axis.set(xlabel="Baseband offset (MHz)", ylabel="FFT frame", title=path.name)
    fig.colorbar(image, ax=axis, label="Power (dBFS)")
    fig.tight_layout()
    fig.savefig(waterfall_path, dpi=150)
    plt.close(fig)

    report_path.write_text(report_text(path, metrics), encoding="utf-8")
    outputs = [spectrum_path, waterfall_path, report_path]
    if write_csv:
        csv_path = path.with_name(path.stem + "_spectrum.csv")
        with csv_path.open("w", newline="", encoding="utf-8") as handle:
            writer = csv.writer(handle)
            writer.writerow(("offset_hz", "power_dbfs"))
            writer.writerows(zip(metrics["frequencies"], metrics["spectrum_dbfs"]))
        outputs.append(csv_path)
    return outputs


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--rate", type=int, required=True)
    parser.add_argument("--fft-size", type=int, default=2048, choices=(1024, 2048, 4096))
    parser.add_argument("--csv", action="store_true")
    args = parser.parse_args()
    metrics = analyze_capture(args.capture, args.rate, fft_size=args.fft_size)
    outputs = write_artifacts(args.capture, metrics, args.csv)
    print(report_text(args.capture, metrics), end="")
    for output in outputs:
        print(f"OUTPUT={output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
