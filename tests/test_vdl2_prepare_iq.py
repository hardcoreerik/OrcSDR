#!/usr/bin/env python3
"""Synthetic input and capture-container tests; not a VDL2 demodulator test."""
import importlib.util
import json
import math
import struct
import tempfile
from pathlib import Path

import numpy as np

path = Path(__file__).resolve().parents[1] / 'tools' / 'vdl2_prepare_iq.py'
spec = importlib.util.spec_from_file_location('vdl2_prepare_iq', path)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)


def sample(rate=250000, tone=25000, count=50000):
    t = np.arange(count)/rate
    wave = 0.48 * np.exp(2j * np.pi * tone * t)
    u8 = np.empty((count, 2), dtype=np.uint8)
    u8[:, 0] = np.clip(np.rint(wave.real*127.5+127.5),0,255)
    u8[:, 1] = np.clip(np.rint(wave.imag*127.5+127.5),0,255)
    return u8.tobytes()


def test_conversion():
    with tempfile.TemporaryDirectory() as temp:
        base=Path(temp); src=base/'raw.cu8'; src.write_bytes(sample())
        output=base/'out.cu8'
        metadata, command=mod.prepare(src,output,250000,136950000,136975000)
        assert metadata['source_sample_rate_sps']==250000
        assert metadata['target_sample_rate_sps']==105000
        assert output.stat().st_size==len(src.read_bytes())*21//50
        assert '--oversample 1' in command
        meta=json.loads((base/'out.cu8.json').read_text()); assert meta['no_decoder_run']
        cu8=np.frombuffer(output.read_bytes(), dtype='u1').astype('f4').reshape(-1,2)
        z=(cu8[:,0]-127.5)+1j*(cu8[:,1]-127.5)
        # Avoid the first and last 100 transient samples. Check the actual signal tone.
        z=z[100:-100]
        freq=np.fft.fftfreq(len(z),d=1/105000)
        peak=freq[np.argmax(np.abs(np.fft.fft(z)))]
        assert abs(peak-25000)<10,(peak,len(z))
        # File must never overwrite existing captures.
        try: mod.prepare(src,output,250000,136950000,136975000)
        except FileExistsError: pass
        else: assert False,'overwrote file'
        # Reject an off-center channel that resampling would alias.
        try: mod.prepare(src,base/'bad.cu8',250000,136880000,136975000)
        except ValueError: pass
        else: assert False,'allowed out-of-band channel'
        # ORCIQ header detection and strict matching.
        container=base/'tab5.orciq'
        raw=src.read_bytes()
        hdr=struct.pack('<8sIIIIHBBII',mod.MAGIC,36,250000,136950000,len(raw),1,7,0,125000,0)
        container.write_bytes(hdr+raw)
        out2=base/'tab5_105.cu8'
        md,_=mod.prepare(container,out2,None,None,136975000)
        assert md['source_format']=='orciq_cu8'
        assert output.read_bytes()==out2.read_bytes()
        try: mod.load_iq(container,250000,136975000)
        except ValueError: pass
        else: assert False,'ignored declared center mismatch'
        print('PASS: CU8 250k->105k, signal frequency, metadata, overwrite guard, ORCIQ')

if __name__=='__main__': test_conversion()
