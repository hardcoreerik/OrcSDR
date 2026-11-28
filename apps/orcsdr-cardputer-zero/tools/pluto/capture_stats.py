"""Capture a channel and report signal health: level, clipping, pilot offset.

The live channels show a strong pilot yet do not decode, so the useful question is
whether the signal arriving at the ADC is usable at all. A 20 m wire at UHF can easily
deliver enough to clip the AD9361, and a clipped 8-VSB signal never locks.

Run: capture_stats.py [centre_hz] [gain_db]
"""
import subprocess
import sys

import numpy as np

CENTRE = float(sys.argv[1]) if len(sys.argv) > 1 else 557e6
GAIN = sys.argv[2] if len(sys.argv) > 2 else "40"
RATE = 10762238
RAW = "/tmp/stats_cap.iq"


def set_attr(channel, attr, value):
    subprocess.run(["iio_attr", "-u", "local:", "-c", "ad9361-phy", channel, attr, value],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)


def main():
    set_attr("voltage0", "sampling_frequency", str(RATE))
    set_attr("voltage0", "rf_bandwidth", str(RATE))
    set_attr("voltage0", "gain_control_mode", "manual")
    set_attr("voltage0", "hardwaregain", GAIN)
    set_attr("altvoltage0", "frequency", str(int(CENTRE)))

    with open(RAW, "wb") as handle:
        subprocess.run(["timeout", "1", "iio_readdev", "-u", "local:", "-b", "65536",
                        "cf-ad9361-lpc", "voltage0", "voltage1"],
                       stdout=handle, stderr=subprocess.DEVNULL, check=False)

    raw = np.fromfile(RAW, dtype=np.int16)
    count = (raw.size // 2) * 2
    if count < 4096:
        print("no data")
        return
    iq = raw[:count].reshape(-1, 2)
    peak = int(np.max(np.abs(iq)))
    clipped = float(np.mean(np.abs(iq) >= 2047))
    rms = float(np.sqrt(np.mean(iq.astype(np.float64) ** 2)))
    print("centre %.0f MHz  gain %s dB" % (CENTRE / 1e6, GAIN))
    print("  samples %d  rms %.1f  peak %d  clipped %.3f%%"
          % (len(iq), rms, peak, clipped * 100.0))


if __name__ == "__main__":
    main()
