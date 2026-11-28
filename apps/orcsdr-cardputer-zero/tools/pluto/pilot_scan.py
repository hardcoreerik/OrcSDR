#!/usr/bin/env python3
"""Detect an ATSC pilot at a channel centre.

8-VSB carries a narrow unmodulated pilot 309.44 kHz above the channel's lower edge,
which is 2.690559 MHz below the centre of a 6 MHz channel. A discrete peak there is
strong evidence of a real station; noise alone is broad and flat.

This separates "the antenna hears nothing" from "the decoder is broken", which the
decoder no longer is: the receive chain was verified against a third-party reference.

Run: pilot_scan.py [centre_hz ...]
"""
import subprocess
import sys

import numpy as np

RATE = 6250000
PILOT_OFFSET = -2690559.0      # pilot relative to a 6 MHz channel centre
CHANNELS = [float(a) for a in sys.argv[1:]] or [557e6, 491e6, 563e6]
CAPTURE_S = 1.0
RAW = "/tmp/atsc_cap.iq"


def capture(centre_hz):
    subprocess.run(["iio_attr", "-u", "local:", "-c", "ad9361-phy", "voltage0",
                    "sampling_frequency", str(RATE)], check=False,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run(["iio_attr", "-u", "local:", "-c", "ad9361-phy", "voltage0",
                    "rf_bandwidth", str(RATE)], check=False,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run(["iio_attr", "-u", "local:", "-c", "ad9361-phy", "voltage0",
                    "gain_control_mode", "manual"], check=False,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run(["iio_attr", "-u", "local:", "-c", "ad9361-phy", "voltage0",
                    "hardwaregain", "40"], check=False,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run(["iio_attr", "-u", "local:", "-c", "ad9361-phy", "altvoltage0",
                    "frequency", str(int(centre_hz))], check=False,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    with open(RAW, "wb") as handle:
        # Bounded capture; length is whatever fits the requested duration.
        subprocess.run(["timeout", str(int(CAPTURE_S)), "iio_readdev", "-u", "local:",
                        "-b", "65536", "cf-ad9361-lpc", "voltage0", "voltage1"],
                       stdout=handle, stderr=subprocess.DEVNULL, check=False)


def analyse(centre_hz):
    raw = np.fromfile(RAW, dtype=np.int16)
    if raw.size < 1 << 16:
        return None
    iq = raw[: (raw.size // 2) * 2].reshape(-1, 2).astype(np.float32)
    x = iq[:, 0] + 1j * iq[:, 1]

    nfft = 16384
    frames = min(len(x) // nfft, 400)
    if frames < 4:
        return None
    psd = np.zeros(nfft)
    window = np.hanning(nfft)
    for f in range(frames):
        seg = x[f * nfft:(f + 1) * nfft] * window
        psd += np.abs(np.fft.fftshift(np.fft.fft(seg))) ** 2
    psd /= frames
    freqs = np.fft.fftshift(np.fft.fftfreq(nfft, 1.0 / RATE))

    floor = np.median(psd)
    # Look in a narrow window around the expected pilot position.
    mask = np.abs(freqs - PILOT_OFFSET) < 20000
    if not mask.any():
        return None
    peak_index = np.argmax(psd * mask)
    peak = psd[peak_index]
    snr_db = 10.0 * np.log10(peak / floor) if floor > 0 else 0.0
    return float(freqs[peak_index]), snr_db, float(floor)


def main():
    for centre in CHANNELS:
        capture(centre)
        result = analyse(centre)
        if result is None:
            print("%.0f MHz: no data" % (centre / 1e6))
            continue
        freq, snr_db, _ = result
        verdict = "PILOT?" if snr_db > 12.0 else "no pilot"
        print("%.0f MHz: pilot bin %.0f kHz, %.1f dB above floor  -> %s"
              % (centre / 1e6, freq / 1e3, snr_db, verdict), flush=True)


if __name__ == "__main__":
    main()
