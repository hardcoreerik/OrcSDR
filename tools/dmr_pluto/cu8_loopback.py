#!/usr/bin/env python3
"""Loopback proof, no RF: turn a generated call into a 240 kS/s CU8 file in the corpus layout (channel 60 kHz above the stream centre) with
optional noise, so the PRODUCTION receive chain (dv_voice_replay / dv_replay) can decode it. usage: cu8_loopback.py call.npz out.cu8 [snr_db]"""
import sys, json
import numpy as np
from scipy.signal import resample_poly

def to_cu8(baseband48, snr_db=None, seed=1):
    x = resample_poly(baseband48, 5, 1)                      # 48 k -> 240 k
    n = np.arange(len(x))
    x = x * np.exp(2j * np.pi * 60000 * n / 240000.0)        # channel at +60 kHz
    x = np.concatenate([np.zeros(48000, np.complex64), x, np.zeros(48000, np.complex64)])   # 0.2 s of silence either side
    amp = 60.0                                               # counts of the unit-amplitude carrier (corpus captures sit near this)
    y = x * amp
    if snr_db is not None:
        rng = np.random.default_rng(seed)
        sigma = amp / (10 ** (snr_db / 20.0)) / np.sqrt(2)
        y = y + sigma * (rng.standard_normal(len(y)) + 1j * rng.standard_normal(len(y)))
    iq = np.empty(2 * len(y), np.uint8)
    iq[0::2] = np.clip(np.round(y.real + 127.5), 0, 255); iq[1::2] = np.clip(np.round(y.imag + 127.5), 0, 255)
    return iq

if __name__ == '__main__':
    d = np.load(sys.argv[1], allow_pickle=True)
    snr = float(sys.argv[3]) if len(sys.argv) > 3 else None
    to_cu8(d['baseband'], snr).tofile(sys.argv[2])
    print('wrote', sys.argv[2], 'snr_db', snr)
