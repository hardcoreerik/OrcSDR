"""Estimate in-channel SNR and pilot strength for a live ATSC channel.

A strong pilot does not imply a decodable signal: the pilot is a narrow line while the
data is spread across 6 MHz, so a badly matched antenna can show a clear pilot and
still be far below the ~15 dB SNR that 8-VSB needs.

Run: channel_snr.py [centre_hz] [gain_db]
"""
import subprocess
import sys

import numpy as np

CENTRE = float(sys.argv[1]) if len(sys.argv) > 1 else 557e6
GAIN = sys.argv[2] if len(sys.argv) > 2 else "60"
RATE = 10762238
RAW = "/tmp/snr_cap.iq"


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
    if count < 1 << 15:
        print("no data")
        return
    iq = raw[:count].reshape(-1, 2).astype(np.float32)
    x = iq[:, 0] + 1j * iq[:, 1]

    nfft = 8192
    frames = min(len(x) // nfft, 300)
    psd = np.zeros(nfft)
    window = np.hanning(nfft)
    for f in range(frames):
        seg = x[f * nfft:(f + 1) * nfft] * window
        psd += np.abs(np.fft.fftshift(np.fft.fft(seg))) ** 2
    psd /= max(frames, 1)
    freqs = np.fft.fftshift(np.fft.fftfreq(nfft, 1.0 / RATE))

    # 6 MHz channel: +-3 MHz. Noise: the outer 0.6 MHz on either side of the capture.
    channel = psd[np.abs(freqs) < 3.0e6]
    noise = psd[np.abs(freqs) > 4.6e6]
    if noise.size == 0 or channel.size == 0:
        print("bandwidth too small for this estimate")
        return
    channel_power = float(np.mean(channel))
    noise_power = float(np.mean(noise))
    snr_db = 10.0 * np.log10(max(channel_power - noise_power, 1e-9) / max(noise_power, 1e-9))

    pilot = psd[np.abs(freqs + 2690559.0) < 8000]
    pilot_db = (10.0 * np.log10(float(np.max(pilot)) / noise_power)) if pilot.size else 0.0

    print("centre %.0f MHz  gain %s dB" % (CENTRE / 1e6, GAIN))
    print("  noise floor %.1f dB   pilot %.1f dB above noise" % (10 * np.log10(noise_power), pilot_db))
    print("  estimated in-channel SNR %.1f dB   (8-VSB needs about 15 dB)" % snr_db)


if __name__ == "__main__":
    main()
