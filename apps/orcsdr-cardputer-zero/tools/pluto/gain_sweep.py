"""Sweep gain at a channel and report clipping, so decoding is attempted at a level
that is actually usable.

The first live captures ran at 60 dB, where the ADC peaked at 2048 (full scale) while
an empty channel at the same gain peaked at 328. Clipping destroys 8-VSB: the signal
is 18 dB above noise but the waveform is distorted, so atsc_rx never locks.

Run: gain_sweep.py [centre_hz]
"""
import subprocess
import sys

import numpy as np

CENTRE = float(sys.argv[1]) if len(sys.argv) > 1 else 557e6
RATE = 10762238
RAW = "/tmp/gs.iq"


def set_attr(channel, attr, value):
    subprocess.run(["iio_attr", "-u", "local:", "-c", "ad9361-phy", channel, attr, value],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)


def measure(gain):
    set_attr("voltage0", "sampling_frequency", str(RATE))
    set_attr("voltage0", "rf_bandwidth", "5000000")
    set_attr("voltage0", "gain_control_mode", "manual")
    set_attr("voltage0", "hardwaregain", str(gain))
    set_attr("altvoltage0", "frequency", str(int(CENTRE)))
    with open(RAW, "wb") as handle:
        subprocess.run(["timeout", "1", "iio_readdev", "-u", "local:", "-b", "65536",
                        "cf-ad9361-lpc", "voltage0", "voltage1"],
                       stdout=handle, stderr=subprocess.DEVNULL, check=False)
    raw = np.fromfile(RAW, dtype=np.int16)
    count = (raw.size // 2) * 2
    if count < 1 << 15:
        return None
    iq = raw[:count]
    peak = int(np.max(np.abs(iq)))
    clipped = float(np.mean(np.abs(iq) >= 2040)) * 100.0
    rms = float(np.sqrt(np.mean(iq.astype(np.float64) ** 2)))
    return peak, clipped, rms


def main():
    print("%-6s %-6s %-8s %-8s %s" % ("gain", "peak", "clip%", "rms", "usable"))
    for gain in (20, 30, 35, 40, 45, 50, 55, 60):
        result = measure(gain)
        if result is None:
            print("%-6d no data" % gain)
            continue
        peak, clipped, rms = result
        usable = "yes" if clipped < 0.01 and peak < 1900 else "clipping"
        print("%-6d %-6d %-8.3f %-8.1f %s" % (gain, peak, clipped, rms, usable))


if __name__ == "__main__":
    main()
