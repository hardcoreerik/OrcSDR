#!/usr/bin/env python3
"""Independent host-only JS8 Normal hard-tone FEC/CRC verifier.
Input: sparse H specification JSON and saved OrcSDR pre-FEC tone candidates.
No GPL decoder source used. Example:
python3 tools/js8-spec/verify_real_frames.py \
    docs/js8/spec/js8_ldpc_174_87.json \
    docs/js8/results/2026-10-08-sample-40m-180s-002-front-end.json
"""
import json, sys
from pathlib import Path

ALPHABET = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-+"
def decode_base_callsign(value):
    chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ /@"
    suffix = []
    for _ in range(3):
        suffix.append(chars[10+value%27])
        value //= 27
    digit = chars[value%10]
    value //= 10
    second = chars[value%36]
    value //= 36
    first = chars[value]
    return (first + second + digit + "".join(reversed(suffix))).strip()

def inspect(frame, checks):
    tones = frame["tones"]
    assert len(tones) == 79 and all(c in "01234567" for c in tones)
    data = tones[7:36] + tones[43:72]
    bits = [int(b) for t in data for b in f"{int(t):03b}"]
    assert len(bits) == 174
    syndrome = sum(sum(bits[j] for j in row) % 2 for row in checks)
    info = bits[87:174]
    text = "".join(ALPHABET[int("".join(map(str,info[i:i+6])),2)] for i in range(0,72,6))
    frame_type = int("".join(map(str,info[72:75])),2)
    expected_crc = int("".join(map(str,info[75:87])),2)
    remainder = 0
    # Compute the non-reflected 12-bit remainder of the 75 information bits,
    # extended by 13 zero bits to cover the 88-bit CRC input byte array.
    for bit in info[:75]+[0]*13:
        top = (remainder >> 11) & 1
        remainder = ((remainder << 1) & 4095) | bit
        if top:
            remainder ^= 0xC06
    calculated_crc = remainder ^ 0x02A
    direction = None
    if syndrome == 0 and expected_crc == calculated_crc and frame_type == 3:
        payload = info[:72]
        intval = lambda start, width: int("".join(map(str,payload[start:start+width])),2)
        value_to_call = lambda v: decode_base_callsign(v)
        src = value_to_call(intval(3,28))
        dest = value_to_call(intval(31,28))
        command_id = intval(59,5)
        extra = intval(64,8)
        command_name = {29:"HEARTBEAT SNR",25:"SNR"}.get(command_id,"UNRESOLVED")
        direction = dict(source=src,destination=dest,command_id=command_id,
                         command=command_name,extra=extra,
                         numeric_snr=extra-31 if command_id in (25,29) and extra else None)
    sync = "4256130"
    sync_blocks = [sum(a == b for a,b in zip(tones[p:p+7],sync)) for p in (0,36,72)]
    return dict(directed=direction,reference_index=frame["ref_index"],audio_hz=frame["audio_hz"],
                start_sample=frame["frame_start_sample"],sync_blocks=sync_blocks,
                pre_correction_syndrome_weight=syndrome,post_correction_syndrome_weight=syndrome,
                fec_corrections_applied=0,crc_expected=expected_crc,crc_computed=calculated_crc,
                crc_pass=expected_crc == calculated_crc,
                inner_12_character_payload=text if syndrome==0 and expected_crc==calculated_crc else None,
                frame_type=frame_type if syndrome==0 and expected_crc==calculated_crc else None)

def main():
    graph = json.loads(Path(sys.argv[1]).read_text())
    frames = json.loads(Path(sys.argv[2]).read_text())
    checks=graph["check_rows"]
    assert len(checks)==87 and all(r[0]==i and all(0<=j<174 for j in r) for i,r in enumerate(checks))
    results=[inspect(f,checks) for f in frames["result"]["raw_frames"]]
    report={"schema":"orcsdr.js8.raw-fec-verification/1",
        "limitations":"No LDPC error correction, no JS8 text/heartbeat reconstruction. Reference indexes come from earlier capture labels.",
        "candidate_count":len(results),"fec_zero_syndrome_count":sum(r["pre_correction_syndrome_weight"]==0 for r in results),
        "crc_pass_count":sum(r["crc_pass"] for r in results),"results":results}
    print(json.dumps(report,indent=2))
    assert any(r["reference_index"]==0 and r["start_sample"]==112320
               and r["pre_correction_syndrome_weight"]==0 and r["crc_pass"]
               and r["inner_12_character_payload"]=="UvnVIpm34Fqg"
               and r["frame_type"]==3 and r["directed"]=={"source":"WO7I","destination":"ND7M","command_id":29,"command":"HEARTBEAT SNR","extra":42,"numeric_snr":11} for r in results)
if __name__=="__main__":
    main()
