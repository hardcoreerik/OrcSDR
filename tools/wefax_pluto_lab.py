#!/usr/bin/env python3
"""Deterministic WEFAX audio source and guarded Pluto conducted-RF test driver.

Default operations never contact or transmit through an SDR. The only RF path is
an explicit 'transmit' command with mandatory equipment and power confirmations.
This is a test source, NOT a qualified/continuous WEFAX broadcast transmitter.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import sys
import time
import wave

BLACK_HZ = 1500.0
WHITE_HZ = 2300.0
AUDIO_RATE = 48000
RF_RATE = 2_400_000


def width_for_ioc(ioc: int) -> int:
    return {288: 905, 576: 1810}[ioc]


def reference_image(np, ioc: int, rows: int):
    """High-contrast deterministic chart; no third-party images or fonts."""
    width = width_for_ioc(ioc)
    x = np.arange(width, dtype=np.int32)[None, :]
    y = np.arange(rows, dtype=np.int32)[:, None]
    image = np.broadcast_to((x * 255 // max(1, width - 1)).astype(np.uint8), (rows, width)).copy()
    image[(y % 6 == 0).ravel(), :] = 0
    image[(y % 6 == 1).ravel(), :] = 255
    image[(y % 6 == 2).ravel(), :] = np.where(x < width // 2, 0, 255)
    image[(y % 6 == 3).ravel(), :] = np.where(((x // 45) % 2) == 0, 0, 255)
    image[(y % 6 == 4).ravel(), :] = np.where((x >= 15) & (x < width - 15), 220, 0)
    return image


def stages(start_s: float, phase_s: float, rows: int, lpm: int, stop_s: float):
    image_s = rows * 60.0 / lpm
    return start_s, start_s + phase_s, start_s + phase_s + image_s, start_s + phase_s + image_s + stop_s


def video_levels(np, ts, image, ioc, lpm, start_s, phase_s, stop_s):
    """APT is modulation of VIDEO, not a 300-Hz audio subcarrier.

    IOC select: alternating black/white video at 300 or 675 Hz.
    Phasing: white lines with a short black segment at each line start.
    Stop: alternating black/white video at 450 Hz.
    """
    rows, width = image.shape
    a, b, c, _ = stages(start_s, phase_s, rows, lpm, stop_s)
    lum = np.zeros(ts.shape, dtype=np.uint8)
    start = ts < a
    lum[start] = np.where(np.floor(ts[start] * (300 if ioc == 576 else 675)) % 2 == 0, 0, 255)
    ph = (ts >= a) & (ts < b)
    phpos = ((ts[ph] - a) * lpm / 60.0) % 1.0
    lum[ph] = np.where(phpos < 0.05, 0, 255)
    data = (ts >= b) & (ts < c)
    if np.any(data):
        p = (ts[data] - b) * lpm / 60.0
        rr = np.floor(p).astype(np.int64)
        xx = np.floor((p - rr) * width).astype(np.int64)
        lum[data] = image[np.minimum(rows - 1, rr), np.clip(xx, 0, width - 1)]
    stop = ts >= c
    lum[stop] = np.where(np.floor((ts[stop] - c) * 450) % 2 == 0, 0, 255)
    return lum


def generate(np, args, out: Path, log):
    out.mkdir(parents=True, exist_ok=True)
    image = reference_image(np, args.ioc, args.rows)
    image_file = out / 'wefax_reference.pgm'
    with image_file.open('wb') as f:
        f.write(f'P5\n{image.shape[1]} {image.shape[0]}\n255\n'.encode('ascii'))
        f.write(image.tobytes())
    wav_file = out / 'wefax_reference_48k.wav'
    total = stages(args.start_seconds, args.phasing_seconds, args.rows, args.lpm, args.stop_seconds)[-1]
    phase = 0.0
    with wave.open(str(wav_file), 'wb') as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(AUDIO_RATE)
        step = AUDIO_RATE // 10
        for offset in range(0, int(round(total * AUDIO_RATE)), step):
            n = min(step, int(round(total * AUDIO_RATE)) - offset)
            t = (offset + np.arange(n, dtype=np.float64)) / AUDIO_RATE
            lum = video_levels(np, t, image, args.ioc, args.lpm, args.start_seconds, args.phasing_seconds, args.stop_seconds)
            hz = BLACK_HZ + (WHITE_HZ - BLACK_HZ) * lum.astype(np.float64) / 255.0
            angles = phase + (2.0 * np.pi / AUDIO_RATE) * np.cumsum(hz, dtype=np.float64)
            phase = float(angles[-1] % (2 * np.pi))
            pcm = np.round(23000 * np.sin(angles)).astype('<i2')
            wf.writeframes(pcm.tobytes())
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    manifest = {'format': 'OrcSDR WEFAX conducted reference v1', 'ioc': args.ioc,
                'lpm': args.lpm, 'rows': args.rows, 'width': image.shape[1],
                'start_seconds': args.start_seconds, 'phasing_seconds': args.phasing_seconds,
                'stop_seconds': args.stop_seconds, 'image_begins_seconds': args.start_seconds + args.phasing_seconds,
                'duration_seconds': total, 'pcm_rate_hz': AUDIO_RATE,
                'tone_black_hz': BLACK_HZ, 'tone_white_hz': WHITE_HZ,
                'wav_sha256': sha(wav_file), 'pgm_sha256': sha(image_file),
                'real_rf_result': 'NOT_TESTED'}
    (out / 'wefax_reference.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    log(f'Generated {wav_file} ({total:.3f}s, sha256={manifest["wav_sha256"]})')
    log(f'Image {image_file}, width={image.shape[1]}, rows={args.rows}')
    return image, manifest


def iio_channel_for_tx_lo(sdr):
    ctx = getattr(sdr, '_ctx', None)
    if ctx is None:
        raise RuntimeError('libiio context inaccessible; refusing TX')
    dev = ctx.find_device('ad9361-phy')
    if dev is None:
        raise RuntimeError('AD9361 PHY missing; refusing TX')
    ch = dev.find_channel('altvoltage1', True)
    if ch is None or 'powerdown' not in ch.attrs:
        raise RuntimeError('TX LO powerdown readback unavailable; refusing TX')
    return ch


def lo_set_and_verify(ch, disabled: bool):
    value = '1' if disabled else '0'
    ch.attrs['powerdown'].value = value
    got = str(ch.attrs['powerdown'].value).strip()
    if got != value:
        raise RuntimeError(f'TX LO powerdown readback mismatch: expected {value}, read {got}')


def transmit(np, args, image, manifest, log):
    if args.confirm_chain != 'TX1-30DB-PAD-RX':
        raise ValueError('TX blocked: --confirm-chain TX1-30DB-PAD-RX is required')
    if not args.expected_serial:
        raise ValueError('TX blocked: provide --expected-serial from live device readback')
    if not args.pluto_uri.startswith(('ip:', 'usb:')):
        raise ValueError('TX blocked: explicit ip:/usb: Pluto URI required')
    if args.measured_input_dbm is None or args.measured_input_dbm > -50:
        raise ValueError('TX blocked: independent receiver-end measurement <= -50 dBm required')
    if not (40.0 <= args.attenuation_db <= 89.75):
        raise ValueError('TX blocked: attenuation must be 40-89.75 dB')
    if not (0 < args.drive <= 0.5):
        raise ValueError('TX blocked: drive must be in (0, 0.5]')
    if args.frequency_hz < 70_000_000 or args.frequency_hz > 6_000_000_000:
        raise ValueError('RF tune outside lab guard; use known licensed conducted frequency')
    # Import only on explicitly confirmed transmit; generate/selftest need no radio.
    import adi
    radio = adi.Pluto(uri=args.pluto_uri)
    txlo = None
    owned = False
    result = {'rf_test': 'STARTED_NOT_VALIDATED', 'rf_hz': args.frequency_hz,
              'uri': args.pluto_uri, 'expected_serial': args.expected_serial,
              'attenuation_db': args.attenuation_db, 'drive': args.drive,
              'measured_input_dbm_operator': args.measured_input_dbm,
              'dma_counter_verification': 'UNAVAILABLE_NOT_A_PASS', 'completed': False}
    try:
        attrs = getattr(radio._ctx, 'attrs', {})
        hardware_metadata = ' '.join(str(getattr(v, 'value', v)) for v in attrs.values())
        if args.expected_serial not in hardware_metadata:
            raise RuntimeError('Live Pluto serial missing from context metadata; no TX access permitted')
        txlo = iio_channel_for_tx_lo(radio)
        lo_set_and_verify(txlo, True)
        owned = True
        radio.tx_cyclic_buffer = False
        radio.sample_rate = RF_RATE
        radio.tx_rf_bandwidth = 200000
        radio.tx_lo = int(args.frequency_hz)
        radio.tx_hardwaregain_chan0 = -float(args.attenuation_db)
        if abs(float(radio.tx_hardwaregain_chan0) + args.attenuation_db) > 0.3:
            raise RuntimeError('TX attenuation readback mismatch')
        if int(radio.tx_lo) != args.frequency_hz:
            raise RuntimeError('TX frequency readback mismatch')
        log('Preflight RF readback passed. RF remains muted until first buffer.')
        log('WARNING: non-cyclic pyadi TX may create DMA gaps; treat any received decode as exploratory.')
        lo_set_and_verify(txlo, False)
        phase = 0.0
        total = float(manifest['duration_seconds'])
        samples = int(round(total * RF_RATE))
        block = RF_RATE // 20  # 50 ms: bounded, no full IQ image allocated
        t0 = time.monotonic()
        for offset in range(0, samples, block):
            count = min(block, samples - offset)
            t = (offset + np.arange(count, dtype=np.float64)) / RF_RATE
            lum = video_levels(np, t, image, args.ioc, args.lpm, args.start_seconds, args.phasing_seconds, args.stop_seconds)
            hz = BLACK_HZ + (WHITE_HZ - BLACK_HZ) * lum.astype(np.float64) / 255.0
            angles = phase + (2 * np.pi / RF_RATE) * np.cumsum(hz, dtype=np.float64)
            phase = float(angles[-1] % (2 * np.pi))
            iq = np.exp(1j * angles).astype(np.complex64) * int(round(16384 * args.drive))
            radio.tx(iq)
        elapsed = time.monotonic() - t0
        result['elapsed_s'] = elapsed
        result['expected_duration_s'] = total
        result['completed'] = True
        result['rf_test'] = 'UNQUALIFIED_DMA_NOT_READ_BACK'
        log(f'pyadi pushed all buffers in {elapsed:.3f}s (waveform {total:.3f}s). Not a proof of gapless RF.')
    finally:
        if owned:
            try:
                radio.tx_destroy_buffer()
            except Exception as exc:
                log(f'TX buffer cleanup warning: {exc}')
            try:
                radio.tx_hardwaregain_chan0 = -89.75
                if txlo is not None:
                    lo_set_and_verify(txlo, True)
                    log('TX LO POWERDOWN VERIFIED; gain set -89.75 dB')
            except Exception as exc:
                result['rf_test'] = 'UNSAFE_SHUTDOWN_UNVERIFIED'
                log(f'CRITICAL: TX mute readback failed: {exc}; use local SSTV stop procedure immediately')
                raise
        (Path.cwd() / 'wefax_pluto_tx_result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('command', choices=['generate', 'selftest', 'transmit'])
    ap.add_argument('--out-dir', default='wefax_pluto_output')
    ap.add_argument('--ioc', type=int, choices=[288, 576], default=576)
    ap.add_argument('--lpm', type=int, choices=[60, 90, 120, 240], default=120)
    ap.add_argument('--rows', type=int, default=10)
    ap.add_argument('--start-seconds', type=float, default=5.0)
    ap.add_argument('--phasing-seconds', type=float, default=30.0)
    ap.add_argument('--stop-seconds', type=float, default=5.0)
    ap.add_argument('--pluto-uri', default='')
    ap.add_argument('--expected-serial', default='')
    ap.add_argument('--frequency-hz', type=int, default=145500000)
    ap.add_argument('--attenuation-db', type=float, default=89.75)
    ap.add_argument('--drive', type=float, default=0.125)
    ap.add_argument('--confirm-chain', default='')
    ap.add_argument('--measured-input-dbm', type=float)
    args = ap.parse_args(argv)
    if args.rows < 2 or args.rows > 1500 or min(args.start_seconds, args.phasing_seconds, args.stop_seconds) <= 0:
        ap.error('rows must be 2..1500 and all stage durations positive')
    if stages(args.start_seconds, args.phasing_seconds, args.rows, args.lpm, args.stop_seconds)[-1] > 60:
        ap.error('reference exceeds 60-second SSTV REC window; reduce rows/durations')
    logpath = Path.cwd() / 'wefax_pluto_lab.log'
    with logpath.open('a', encoding='utf-8') as logfile:
        def log(s):
            line = f'[{time.strftime("%Y-%m-%dT%H:%M:%S%z")}] {s}'
            print(line, flush=True)
            logfile.write(line + '\n')
            logfile.flush()
        log(f'command={args.command}, log={logpath.resolve()}')
        try:
            import numpy as np
            image, manifest = generate(np, args, Path(args.out_dir), log)
            if args.command == 'selftest':
                with wave.open(str(Path(args.out_dir) / 'wefax_reference_48k.wav'), 'rb') as wf:
                    assert wf.getframerate() == AUDIO_RATE and wf.getnchannels() == 1
                    assert wf.getnframes() == int(round(manifest['duration_seconds'] * AUDIO_RATE))
                    assert wf.getsampwidth() == 2
                assert len(image) == args.rows and image.shape[1] == width_for_ioc(args.ioc)
                assert image[0, 0] == 0 and image[1, 0] == 255
                log('SELFTEST PASS: WAV frame count, reference image dimensions and row patterns')
            elif args.command == 'transmit':
                transmit(np, args, image, manifest, log)
            log('finished')
            return 0
        except Exception as exc:
            log(f'ERROR: {type(exc).__name__}: {exc}')
            return 1


if __name__ == '__main__':
    sys.exit(main())
