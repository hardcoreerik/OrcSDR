#!/usr/bin/env python3
"""Prepare capture-only CU8 IQ for dumpvdl2 105 kS/s replay (host tool).

Reads headerless RTL-SDR CU8 or an OrcSDR ORCIQ01 capture, preserves the RF
center-frequency metadata, resamples the complex stream (with anti-aliasing)
and exports headerless CU8 at exactly 105000 samples/second.

No IQ demodulation or protocol decoding is performed. Requires numpy/scipy.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import shlex
import struct
import sys
import tempfile
from fractions import Fraction
from pathlib import Path

ORCIQ_HEADER = struct.Struct('<8sIIIIHBBII')
MAGIC = b'ORCIQ01\0'
OUTPUT_RATE = 105000
# Deliberate host-side safety bound. Peak memory is several times the input: the CU8 bytes, a complex64 copy (4x the bytes) and the
# filtered output, so a capture at this bound needs a host with about 2 GiB of free memory. Split larger captures first.
MAX_INPUT_BYTES = 256 * 1024 * 1024


def load_iq(path: Path, sample_rate: int | None, center_hz: int | None):
    if path.stat().st_size > MAX_INPUT_BYTES + ORCIQ_HEADER.size:   # refuse before reading it all into memory
        raise ValueError('Capture exceeds the 256 MiB host processing bound; split it first')
    content = path.read_bytes()
    if not content:
        raise ValueError('Capture is empty')
    kind = 'raw_cu8'
    if content[:8] == MAGIC:
        if len(content) < ORCIQ_HEADER.size:
            raise ValueError('Truncated OrcSDR ORCIQ header')
        magic, hsize, rate, center, nbytes, fmt, _sf, _reserved, _bw, flags = ORCIQ_HEADER.unpack_from(content)
        if magic != MAGIC or hsize != ORCIQ_HEADER.size or fmt != 1 or flags != 0:
            raise ValueError('Unsupported OrcSDR ORCIQ capture header')
        if len(content) - hsize != nbytes:
            raise ValueError('ORCIQ declared bytes do not match actual data')
        kind = 'orciq_cu8'
        content = content[hsize:]
        if sample_rate is not None and sample_rate != rate:
            raise ValueError('Supplied sample rate differs from ORCIQ header')
        if center_hz is not None and center_hz != center:
            raise ValueError('Supplied center frequency differs from ORCIQ header')
        sample_rate, center_hz = rate, center
    if sample_rate is None or center_hz is None:
        raise ValueError('Raw CU8 needs --rate and --center-hz; ORCIQ may provide these')
    if sample_rate < 105000 or sample_rate > 4_000_000:
        raise ValueError('Supported source IQ rate is 105000..4000000 samples/second')
    if center_hz < 1 or center_hz > 3_000_000_000:
        raise ValueError('Invalid capture center frequency')
    if len(content) % 2:
        raise ValueError('CU8 must have an even number of interleaved I/Q bytes')
    if len(content) > MAX_INPUT_BYTES:
        raise ValueError('Capture exceeds 256 MiB host processing bound; split it first')
    return content, sample_rate, center_hz, kind


def convert(raw: bytes, source_rate: int) -> bytes:
    import numpy as np
    from scipy.signal import resample_poly

    samples = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 2)
    complex_samples = ((samples[:, 0].astype(np.float32) - 127.5) +
                       1j * (samples[:, 1].astype(np.float32) - 127.5)) / 127.5
    ratio = Fraction(OUTPUT_RATE, source_rate)
    if ratio.numerator == ratio.denominator:
        return raw
    filtered = resample_poly(complex_samples, ratio.numerator, ratio.denominator)
    output = np.empty((len(filtered), 2), dtype=np.uint8)
    output[:, 0] = np.clip(np.rint(np.real(filtered) * 127.5 + 127.5), 0, 255)
    output[:, 1] = np.clip(np.rint(np.imag(filtered) * 127.5 + 127.5), 0, 255)
    return output.tobytes()


def publish_exclusive(src: Path, dst: Path) -> None:
    """Make `dst` appear with the contents of `src`, failing with FileExistsError instead of replacing an existing file."""
    try:
        os.link(src, dst)
    except FileExistsError:
        raise
    except (OSError, NotImplementedError):                 # a file system without hard links: create exclusively and copy
        with src.open('rb') as source, dst.open('xb') as target:
            target.write(source.read())


def prepare(input_path: Path, output_path: Path, rate: int | None,
            center_hz: int | None, channel_hz: int):
    raw, rate, center_hz, kind = load_iq(input_path, rate, center_hz)
    if abs(channel_hz - center_hz) >= OUTPUT_RATE // 2 - 10_000:
        raise ValueError('VDL2 channel is too close to output Nyquist edge; '
                         'record closer to the desired channel center')
    metadata_path = output_path.with_suffix(output_path.suffix + '.json')
    if output_path.exists() and not metadata_path.exists():
        output_path.unlink()               # an output without its sidecar is what an interrupted run leaves behind: retry cleanly
    if output_path.exists() or metadata_path.exists():
        raise FileExistsError('Output and metadata already exist; refusing to overwrite')
    payload = convert(raw, rate)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    metadata = {
        'capture_origin': str(input_path), 'source_format': kind,
        'source_sample_rate_sps': rate, 'target_sample_rate_sps': OUTPUT_RATE,
        'center_frequency_hz': center_hz, 'vdl2_channel_frequency_hz': channel_hz,
        'source_duration_s': len(raw) / 2 / rate, 'output_iq_bytes': len(payload),
        'output_sha256': hashlib.sha256(payload).hexdigest(),
        'processing': 'scipy.signal.resample_poly complex IQ; quantized CU8',
        'no_decoder_run': True,
    }
    staged = []                            # temporary files of this invocation
    created = []                           # final files this invocation published (and so may remove on failure)
    try:
        for data, final in ((payload, output_path), (json.dumps(metadata, indent=2).encode('utf-8') + b'\n', metadata_path)):
            fd, tmp_name = tempfile.mkstemp(prefix=final.name + '.', suffix='.tmp', dir=final.parent)   # unique per invocation
            staged.append(Path(tmp_name))
            with os.fdopen(fd, 'wb') as out:
                out.write(data)
        for tmp, final in zip(staged, (output_path, metadata_path)):
            publish_exclusive(tmp, final)  # raises FileExistsError if another run got there first: never replaces its file
            created.append(final)
    except BaseException:
        for final in created:              # only what this invocation created
            final.unlink(missing_ok=True)
        raise
    finally:
        for tmp in staged:
            tmp.unlink(missing_ok=True)
    run_command = ('dumpvdl2 --iq-file ' + shlex.quote(str(output_path)) +
                   ' --sample-format U8 --oversample 1 --centerfreq ' +
                   str(center_hz) + ' ' + str(channel_hz))
    return metadata, run_command


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('capture', type=Path, help='Existing headerless CU8 or ORCIQ01 capture')
    p.add_argument('--rate', type=int, help='Original complex samples/second (raw CU8 only)')
    p.add_argument('--center-hz', type=int, help='Original receiver center frequency (raw CU8 only)')
    p.add_argument('--channel-hz', type=int, default=136975000,
                   help='VDL2 RF frequency (default 136975000)')
    p.add_argument('--output', type=Path, help='105k CU8 output path; defaults next to original')
    args = p.parse_args(argv)
    output = args.output or args.capture.with_name(args.capture.stem + '_105k.cu8')
    try:
        meta, command = prepare(args.capture, output, args.rate, args.center_hz, args.channel_hz)
    except (ValueError, OSError) as error:
        print(f'ERROR: {error}', file=sys.stderr)
        return 2
    print('Captured I/Q prepared: ' + str(output))
    print('Samples: {:.3f}s from {:,} -> {:,} S/s'.format(
        meta['source_duration_s'], meta['source_sample_rate_sps'], OUTPUT_RATE))
    print('Metadata: ' + str(output.with_suffix(output.suffix + '.json')))
    print('Reference decode (Linux/WSL dumpvdl2):\n' + command)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
