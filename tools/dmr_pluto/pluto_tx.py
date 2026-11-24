#!/usr/bin/env python3
"""Conducted DMR test transmission from the lab Pluto, through OrcAnomaly's safety layer (band limit, attenuation floor, readback, silence on
every exit). CABLE ONLY: TX1 -> attenuator(s) -> receiver input. Never an antenna on the TX port. The script refuses to transmit unless the
operator states, on the command line, exactly what is between TX1 and the receiver, and shows the expected level first.

  --dry-run                       build the buffer, print the plan and the expected level, transmit nothing (needs no Pluto, no iio)
  real run (needs all of):        --wiring "TX1 -> 30 dB pad -> SMA cable -> V4L"  --physical-pad-db 30  --atten-db 60  --seconds 30
                                  --yes-conducted-no-antenna

Frequency: the carrier must lie inside the ISM window OrcAnomaly enforces (902.2-927.8 MHz), so even stray leakage stays in an unlicensed band
at very low level. LO feedthrough lands 150 kHz below the carrier, the image 300 kHz below it.
"""
import argparse, hashlib, json, sys, time
from pathlib import Path
import numpy as np
from scipy.signal import resample_poly

FS = 2_400_000
BAND = (902.2e6, 927.8e6)
MIN_ATTEN_DB = 20.0
PA_DBM = 19.0            # this board's onboard PA, about +19 dBm at 0 dB attenuation (memory: feedback_pluto_tx_safety)
BACKOFF_DB = 6.0         # waveform at half DAC full scale
MAX_SECONDS = 60.0       # cyclic transmit is bounded to 60 s unattended by the firmware

def build_buffer(baseband48, offset_hz, gap_s=1.0, level=0.5):
    x = resample_poly(baseband48, FS // 48000, 1).astype(np.complex64)
    n = np.arange(len(x))
    x = x * np.exp(2j * np.pi * offset_hz * n / FS).astype(np.complex64)
    x = np.concatenate([x, np.zeros(int(gap_s * FS), np.complex64)])     # a quiet gap between calls
    iq = np.empty(2 * len(x), np.int16)
    iq[0::2] = np.round(x.real * level * 32767); iq[1::2] = np.round(x.imag * level * 32767)   # TX full scale is +-32767
    return iq

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('call'); ap.add_argument('--channel-hz', type=float, default=915.0e6)
    ap.add_argument('--offset-hz', type=float, default=150e3); ap.add_argument('--atten-db', type=float, default=60.0)
    ap.add_argument('--physical-pad-db', type=float, default=None); ap.add_argument('--wiring', default=None)
    ap.add_argument('--seconds', type=float, default=30.0); ap.add_argument('--uri', default='ip:192.168.2.1')
    ap.add_argument('--yes-conducted-no-antenna', action='store_true'); ap.add_argument('--dry-run', action='store_true')
    ap.add_argument('--log', default=None)
    a = ap.parse_args()
    d = np.load(a.call, allow_pickle=True)
    label = json.loads(str(d['label']))
    lo = a.channel_hz - a.offset_hz
    if not (BAND[0] <= lo <= BAND[1] and BAND[0] <= a.channel_hz <= BAND[1]):
        sys.exit(f'refused: carrier {a.channel_hz/1e6:.4f} MHz / LO {lo/1e6:.4f} MHz outside {BAND[0]/1e6}-{BAND[1]/1e6} MHz')
    if a.atten_db < MIN_ATTEN_DB: sys.exit(f'refused: attenuation {a.atten_db} dB below the {MIN_ATTEN_DB} dB floor')
    if a.seconds > MAX_SECONDS or a.seconds <= 0: sys.exit(f'refused: --seconds must be 0..{MAX_SECONDS}')
    iq = build_buffer(d['baseband'], a.offset_hz)
    sha = hashlib.sha256(iq.tobytes()).hexdigest()
    pad = a.physical_pad_db if a.physical_pad_db is not None else 0.0
    level = PA_DBM - a.atten_db - BACKOFF_DB - pad
    cycle_s = len(iq) / 2 / FS
    plan = dict(call_seconds=label['seconds'], cycle_seconds=round(cycle_s, 3), calls_in_run=int(a.seconds // cycle_s),
                channel_mhz=a.channel_hz / 1e6, lo_mhz=lo / 1e6, atten_db=a.atten_db, physical_pad_db=a.physical_pad_db,
                expected_level_at_receiver_dbm_estimate=round(level, 1), buffer_bytes=len(iq) * 2, buffer_sha256=sha,
                src=label['src'], dst=label['dst'], cc=label['cc'], slot=label['slot'], frames_per_call=len(label['frames_sent']))
    print(json.dumps(plan, indent=1))
    print('NOTE: the level is an estimate (PA about +19 dBm at 0 dB, waveform 6 dB below full scale, minus programmed attenuation and the pad).')
    if a.dry_run:
        print('dry run: nothing transmitted'); return
    if not a.yes_conducted_no_antenna or not a.wiring or a.physical_pad_db is None:
        sys.exit('refused: state the wiring (--wiring "...", --physical-pad-db N) and pass --yes-conducted-no-antenna. TX1 goes through a cable and attenuation to the receiver, never to an antenna.')
    if level > -20.0:
        sys.exit(f'refused: expected {level:.1f} dBm at the receiver is above -20 dBm; add attenuation (RTL-SDR input must stay far below +2.5 dBm)')
    sys.path.insert(0, r'F:\Ai\OrcAnomaly')
    from siggen.radio import Pluto, TransmitRefused           # OrcAnomaly's safety layer: band, floor, readback, silence on exit
    radio = Pluto(a.uri, FS, BAND, MIN_ATTEN_DB, tx_buffer_bytes=len(iq) * 2)
    t0 = time.time()
    try:
        print('Pluto identity:', json.dumps(radio.identity()))
        radio.retune(lo)
        radio.set_cyclic_bound_s(min(MAX_SECONDS, a.seconds + 5))
        on = radio.transmit(iq, a.atten_db)
        print(time.strftime('%H:%M:%S'), 'TRANSMITTING (conducted) for', a.seconds, 's; calls start every', round(cycle_s, 2), 's')
        res = radio.hold(a.seconds)
        print('hold result:', json.dumps(res, default=str)[:300])
    finally:
        radio.silence()
        try: radio.restore_cyclic_bound()
        except Exception: pass
        radio.close()
        print(time.strftime('%H:%M:%S'), 'transmitter silenced')
    rec = dict(plan, wiring=a.wiring, started=t0, ended=time.time(), uri=a.uri, call_file=str(a.call))
    if a.log:
        with open(a.log, 'a') as f: f.write(json.dumps(rec) + '\n')

if __name__ == '__main__':
    main()
