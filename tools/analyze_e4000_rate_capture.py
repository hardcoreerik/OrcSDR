"""Verify and measure a saved E4000 custom-rate tone capture. No hardware access.
Usage: python tools/analyze_e4000_rate_capture.py CAPTURE.json EXPECTED_RATE
Requires numpy; IQ file must have the same basename and .cu8 extension.
"""
import hashlib, json, sys
from pathlib import Path
import numpy as np
p = Path(sys.argv[1]); rate = int(sys.argv[2])
m = json.loads(p.read_text(encoding="utf-8-sig")); data = p.with_suffix(".cu8").read_bytes()
assert len(data) == m["capture_bytes"] == rate * 2
assert hashlib.sha256(data).hexdigest() == m["capture_sha256"]
assert m["sample_rate_sps"] == rate
assert m["effective_iq_center_hz"] == 100000000
assert m["hardware_offset_hz"] == rate * 85 // 100
assert m["physical_tuner_lo_hz"] + m["hardware_offset_hz"] == 100000000
assert m["usb_overruns_after"] == m["usb_overruns_before"]
assert m["consumer_drops_after"] == m["consumer_drops_before"]
a = np.frombuffer(data, np.uint8)
z = (a[0::2].astype(float)-127.5 + 1j*(a[1::2].astype(float)-127.5))/128
n = 65536; count = min(16, len(z)//n); window = np.hanning(n)
power = np.mean(abs(np.fft.fft(z[:count*n].reshape(-1,n)*window, axis=1)/window.sum())**2, axis=0)
f = np.fft.fftfreq(n, 1/rate); target = abs(f-100000)<2000
k = np.flatnonzero(target)[np.argmax(power[target])]
floor = np.median(power[(abs(f-100000)>10000)&(abs(f-100000)<50000)])
contrast = float(10*np.log10(power[k]/floor))
assert contrast > 15, contrast
assert not np.any((a==0)|(a==255)), "Rail clipping"
print(json.dumps(dict(pass_test=True, rate=rate, tone_hz=float(f[k]), tone_dbfs=float(10*np.log10(power[k])), local_contrast_db=contrast, physical_lo_hz=m["physical_tuner_lo_hz"], iq_center_hz=m["effective_iq_center_hz"], sha256=m["capture_sha256"]), indent=2))
