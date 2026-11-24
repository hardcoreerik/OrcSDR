#!/usr/bin/env python3
"""Fidelity comparison of NATIVE production-chain audio against (a) the offline V2 sandbox WAVs (same vocoder, offline extractor)
and (b) the DSD-FME per-call WAVs from the reference pack (independent decoder, mbelib 1.3.4). Comparison only: nothing here
feeds the receiver. usage: dv_voice_audio_compare.py native.wav sandbox_v2.wav dsdfme.wav"""
import sys, wave, json
import numpy as np

def load(p):
    w = wave.open(p); assert w.getsampwidth() == 2 and w.getnchannels() == 1
    return np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').astype(np.float64), w.getframerate()

def stats(x):
    n = len(x)
    rms = float(np.sqrt(np.mean(x * x))) if n else 0.0
    return dict(seconds=n / 8000.0, rms=round(rms, 1), peak=int(np.max(np.abs(x))) if n else 0, clipped=int(np.sum(np.abs(x) >= 32760)),
                zero_frames=int(np.sum(np.all(x[:n // 160 * 160].reshape(-1, 160) == 0, axis=1))) if n >= 160 else 0)

def f0_track(x, fs=8000):
    """Per-20 ms autocorrelation pitch (80..400 Hz); 0 = unvoiced/silent. A measurement of continuity, not ground truth."""
    out = []
    for i in range(0, len(x) - 320, 160):
        seg = x[i:i + 320] - np.mean(x[i:i + 320])
        e = np.dot(seg, seg)
        if e < 1e5: out.append(0.0); continue
        ac = np.correlate(seg, seg, 'full')[319:]
        lo, hi = int(fs / 400), int(fs / 80)
        k = lo + int(np.argmax(ac[lo:hi]))
        out.append(fs / k if ac[k] > 0.35 * ac[0] else 0.0)
    return np.array(out)

def envelope(x):
    n = len(x) // 160 * 160
    return np.sqrt(np.mean(x[:n].reshape(-1, 160) ** 2, axis=1))

def main():
    nat, _ = load(sys.argv[1]); sbx, _ = load(sys.argv[2]); ref, _ = load(sys.argv[3])
    res = dict(native=stats(nat), sandbox_v2=stats(sbx), dsdfme=stats(ref))
    n = min(len(nat), len(sbx))
    res['native_vs_sandbox_v2'] = dict(samples_compared=n, identical_samples=int(np.sum(nat[:n] == sbx[:n])), max_abs_diff=int(np.max(np.abs(nat[:n] - sbx[:n]))) if n else 0, length_equal=len(nat) == len(sbx))
    en, er = envelope(nat), envelope(ref)
    m = min(len(en), len(er))
    corr = float(np.corrcoef(en[:m], er[:m])[0, 1]) if m > 2 and np.std(en[:m]) > 0 and np.std(er[:m]) > 0 else None
    fn, fr = f0_track(nat), f0_track(ref)
    m2 = min(len(fn), len(fr)); both = (fn[:m2] > 0) & (fr[:m2] > 0)
    jump = lambda f: float(np.mean(np.abs(np.diff(f[f > 0])) / f[f > 0][:-1] > 0.25)) if np.sum(f > 0) > 3 else None
    res['native_vs_dsdfme'] = dict(frame_envelope_correlation=None if corr is None else round(corr, 3), rms_ratio=round(res['native']['rms'] / max(res['dsdfme']['rms'], 1e-9), 3),
        voiced_frames_native=int(np.sum(fn > 0)), voiced_frames_dsdfme=int(np.sum(fr > 0)), voiced_both=int(np.sum(both)),
        median_f0_ratio_where_both=round(float(np.median(fn[:m2][both] / fr[:m2][both])), 3) if np.any(both) else None,
        f0_jump_rate_native=jump(fn), f0_jump_rate_dsdfme=jump(fr))
    print(json.dumps(res, indent=2))
main()
