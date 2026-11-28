"""Compare channel power against a noise reference taken the same way.

The in-channel SNR estimate was biased: with a 5 MHz analogue filter there is no
unattenuated noise left inside the captured band to measure, so any "noise floor"
taken near the band edges is artificially low and inflates the SNR. The honest
reference is an empty frequency captured with identical settings.

Run: noise_ref.py
"""
import subprocess

import numpy as np

RATE = 10762238
BW = "5000000"
GAIN = "60"
CAPTURE = "/tmp/nr.iq"


def set_attr(channel, attr, value):
    subprocess.run(["iio_attr", "-u", "local:", "-c", "ad9361-phy", channel, attr, value],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)


def measure(centre):
    set_attr("voltage0", "sampling_frequency", str(RATE))
    set_attr("voltage0", "rf_bandwidth", BW)
    set_attr("voltage0", "gain_control_mode", "manual")
    set_attr("voltage0", "hardwaregain", GAIN)
    set_attr("altvoltage0", "frequency", str(int(centre)))
    with open(CAPTURE, "wb") as handle:
        subprocess.run(["timeout", "1", "iio_readdev", "-u", "local:", "-b", "65536",
                        "cf-ad9361-lpc", "voltage0", "voltage1"],
                       stdout=handle, stderr=subprocess.DEVNULL, check=False)
    raw = np.fromfile(CAPTURE, dtype=np.int16)
    count = (raw.size // 2) * 2
    if count < 1 << 15:
        return None
    iq = raw[:count].astype(np.float64)
    return float(np.mean(iq ** 2)), int(np.max(np.abs(iq)))


def main():
    signal, peak_s = measure(557e6)
    noise, peak_n = measure(600e6)
    if signal is None or noise is None:
        print("no data")
        return
    snr_db = 10.0 * np.log10(max(signal - noise, 1e-9) / max(noise, 1e-9))
    print("557 MHz power %.1f dB  peak %d" % (10 * np.log10(signal), peak_s))
    print("600 MHz power %.1f dB  peak %d   (empty channel, same settings)"
          % (10 * np.log10(noise), peak_n))
    print("signal above noise: %.1f dB   (8-VSB needs about 15 dB)" % snr_db)


if __name__ == "__main__":
    main()
