"""CU8 240 kS/s (channel +60 kHz above centre) -> 48 kS/s 16-bit mono WAV of FM discriminator output (rtl_fm-style scaling),
the input third-party decoders such as DSD-FME expect. usage: fm_wav.py in.cu8 out.wav"""
import sys, wave, numpy as np
from scipy import signal
raw = np.fromfile(sys.argv[1], dtype=np.uint8).astype(np.float32)
z = ((raw[0::2] - 127.5) + 1j * (raw[1::2] - 127.5)) / 127.5
n = np.arange(len(z)); z = z * np.exp(-2j * np.pi * (60e3 / 240e3) * n)
z = signal.lfilter(signal.firwin(129, 8000, fs=240000), 1.0, z)[::5]
d = np.angle(z[1:] * np.conj(z[:-1]))
pcm = np.clip(d / np.pi * 32768, -32768, 32767).astype('<i2')
w = wave.open(sys.argv[2], 'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(48000); w.writeframes(pcm.tobytes()); w.close()
print(len(pcm) / 48000, "s", "rms", float(np.sqrt(np.mean(pcm.astype(np.float64) ** 2))))
