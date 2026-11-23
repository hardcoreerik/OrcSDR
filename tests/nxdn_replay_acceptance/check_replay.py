#!/usr/bin/env python3
"""Verify exact real-air NXDN source hashes, run the real OrcSDR decoder.

Upstream reference frame counts are NOT proof of OrcSDR acceptance.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

FIXTURES = (
 ("nxdn48_rdch_voice_447100_16oct_48k.cs16", "48", "26aed395774380be0cf6a0ebdf391723a9335e00"),
 ("nxdn96_rdch_voice_447100_15oct_48k.cs16", "96", "01845523c82df3a279712b577a0f0180653f981f"),
 ("nxdn96_rcch_cc_453775_19oct_48k.cs16", "96", "c4a1bdb2fdd36cc316877982781c99bc7043462d"),
)

def inspect(name,mode,expected,base,replay):
    path=base / name
    data=path.read_bytes()
    digest=hashlib.sha1(b"blob "+str(len(data)).encode()+b"\x00"+data).hexdigest()
    if digest!=expected:
        raise ValueError(f"{path}: Git blob SHA1 mismatch got {digest}, expected {expected}")
    if len(data)%4:
        raise ValueError(f"{path}: truncated CS16 sample")
    runs=[]
    for chunk in (1,37,4096):
        proc=subprocess.run([str(replay.resolve()),mode,str(path),"--frames",f"--chunk={chunk}"],
                            capture_output=True,text=True,check=True)
        events=[json.loads(line) for line in proc.stdout.splitlines() if line]
        runs.append(events)
        if chunk==4096:
            print(proc.stderr.strip(),file=sys.stderr)
    if not all(x==runs[0] for x in runs[1:]):
        raise AssertionError(f"{name}: IQ chunk size changes evidence output")
    frames=[x for x in runs[0] if x["kind"]=="lich_candidate"]
    crc=[x for x in runs[0] if x["kind"]=="sacch_crc_valid"]
    result={"fixture":name,"bytes":len(data),"git_blob_sha1":digest,
            "sha256":hashlib.sha256(data).hexdigest(),
            "fsw_lich_candidates":len(frames),"sacch_crc_valid":len(crc),
            "raw_voice_codeword_candidates":sum(x["voice_codewords_unverified"] for x in frames),
            "note":"raw VCH candidates do not pass vocoder Golay validation"}
    print(json.dumps(result,sort_keys=True))

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--fixtures",type=Path,required=True)
    p.add_argument("--replay",type=Path,required=True)
    a=p.parse_args()
    for name,mode,digest in FIXTURES:
        inspect(name,mode,digest,a.fixtures,a.replay)
if __name__=="__main__":
    main()
