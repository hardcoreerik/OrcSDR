#!/usr/bin/env python3
"""Stage B comparison. Compares the NATIVE production-chain frame lists (output/native/<stem>_ts*_call*.frames.txt, 9-byte
over-the-air AMBE+2 frames) with (a) the offline V2 sandbox frames and (b) the DSD-FME post-FEC AMBE vectors from the
external reference pack. Comparison only: nothing here feeds the native receiver or the vocoder."""
import re, sys, glob, zipfile, difflib, json
import ctypes as C
import numpy as np
from pathlib import Path

def native(out, stem):
    frames = []
    for p in sorted(glob.glob(f"{out}/{stem}_ts*_call*.frames.txt"), key=lambda x: int(re.search(r"_call(\d+)\.", x).group(1))):
        frames += [x.lower() for x in Path(p).read_text().split()]
    return frames

def v2(v2dir, stem):
    frames = []
    items = []
    for p in glob.glob(f"{v2dir}/{stem}_ts*_src*_at*.frames.txt"):
        at = int(re.search(r"_at(\d+)\.", p).group(1))
        items.append((at, p))
    for _, p in sorted(items):
        frames += [l.split()[1] for l in Path(p).read_text().splitlines() if l.strip()]
    return frames

Mat=(C.c_char*24)*4; Dat=C.c_char*49
rw=[0,1]*11+[0,2]*7; rx=[23,10,22,9,21,8,20,7,19,6,18,5,17,4,16,3,15,2,14,1,13,0,12,10,11,9,10,8,9,7,8,6,7,5,6,4]
ry=[0,2,0,2,0,2,0,2,0,3,0,3]+[1,3]*12; rz=[5,3,4,2,3,1,2,0,1,13,0,12,22,11,21,10,20,9,19,8,18,7,17,6,16,5,15,4,14,3,13,2,12,1,11,0]
def params(lib, hexw):
    w = bytes.fromhex(hexw); m = Mat(); data = Dat()
    for i in range(36):
        d = (w[i//4] >> (6-2*(i%4))) & 3; m[rw[i]][rx[i]] = bytes([d>>1]); m[ry[i]][rz[i]] = bytes([d&1])
    lib.mbe_eccAmbe3600x2450C0(C.byref(m)); lib.mbe_demodulateAmbe3600x2450Data(C.byref(m)); lib.mbe_eccAmbe3600x2450Data(C.byref(m), C.byref(data))
    return np.packbits(np.frombuffer(bytes(data), dtype=np.uint8)).tobytes().hex().upper()

def seqmatch(a, b):
    return sum(m.size for m in difflib.SequenceMatcher(None, a, b, autojunk=False).get_matching_blocks())

def main():
    out, v2dir, ref = sys.argv[1], sys.argv[2], sys.argv[3]
    lib = C.CDLL(sys.argv[4])
    z = zipfile.ZipFile(ref)
    res = {}
    for stem, key in (("terra_152p1575_call_cc13", "call1"), ("terra_152p685_call_cc8", "call2"), ("terra_153p068_call_cc8", "call3")):
        n = native(out, stem); s = v2(v2dir, stem)
        name = next(x for x in z.namelist() if "/" + key + "_" in x and x.endswith(".txt"))
        refv = re.findall(r"AMBE ([0-9A-F]{14}) err = \[(\d+)\] \[(\d+)\]", z.read(name).decode())
        # reference frames are post-FEC 49-bit vectors; native/V2 wire frames are 72 channel bits. Compare through the
        # same demodulate+ECC path the V2 proof used: native vs V2 on wire bytes directly, both vs DSD-FME on parameters.
        npar = [params(lib, x) for x in n]; spar = [params(lib, x) for x in s]; rs = [r[0] for r in refv]
        res[stem] = dict(native_params_vs_dsdfme=seqmatch(rs, npar), v2_params_vs_dsdfme=seqmatch(rs, spar), native=len(n), v2=len(s), dsdfme=len(refv), native_vs_v2_exact_sequence=seqmatch(n, s),
                         native_subset_of_v2=len(set(n) - set(s)) == 0, native_not_in_v2=len(set(n) - set(s)), v2_not_in_native=len(set(s) - set(n)))
    print(json.dumps(res, indent=2))
main()
