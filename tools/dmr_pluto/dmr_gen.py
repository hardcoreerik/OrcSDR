#!/usr/bin/env python3
"""DMR base-station downlink test-signal generator for conducted (cabled) tests of the OrcSDR receive chain.

Builds one short DMR call on a chosen slot: voice LC header, N voice superframes (bursts A..F with embedded LC), terminator, with the
other slot idle, as 4FSK at 4800 baud (ETSI TS 102 361-1 framing). Voice payload frames are 72-bit AMBE+2 channel frames taken from a
text file (one 18-hex-digit frame per line); there is NO voice encoder here, so content is whatever frames you give it.

Everything is written from the same standard sections the receiver was built from; the loopback test (cu8_loopback.py) decodes the
output with the production chain, which is the proof that the generator and the receiver agree. This module never touches hardware.

  dmr_gen.py --frames test_frames_18.txt --src 4106008 --dst 6011885 --cc 8 --slot 1 --superframes 8 --out call.npz
"""
import argparse, hashlib, json, sys
import numpy as np

SYNC = {'bs_voice': 0x755FD7DF75F7, 'bs_data': 0xDFF57D75DF5D}
GOLAY_P = [0x3da, 0xd99, 0x6cd, 0x367, 0xdc6, 0xa97, 0x93e, 0x8eb]
H15 = [9, 13, 15, 14, 7, 10, 5, 11, 12, 6, 3]
H13 = [15, 14, 7, 10, 5, 11, 12, 6, 3]
ROW16 = [19, 26, 31, 28, 14, 21, 11, 22, 25, 13, 7]
QR_P = [0x04f, 0x11e, 0x1b7, 0x1e2, 0x1c9, 0x0e5, 0x073]
RS = [[0x1c, 0xbc, 0xfd], [0x89, 0x31, 0x08], [0xad, 0x41, 0x36], [0x7d, 0x71, 0x16], [0xf3, 0xa6, 0x3a],
      [0x08, 0x83, 0x7b], [0x3f, 0x6f, 0x02], [0x6c, 0x0d, 0xa7], [0x0e, 0x38, 0x40]]
DT_VOICE_LC_HEADER, DT_TERMINATOR_LC, DT_IDLE = 1, 2, 9

def bits_of(value, n):
    return [(value >> (n - 1 - i)) & 1 for i in range(n)]

def gf(a, b):
    out = 0
    for _ in range(8):
        if b & 1: out ^= a
        b >>= 1
        hi = a & 0x80
        a = ((a << 1) ^ (0x1d if hi else 0)) & 0xff
    return out

def lc_bytes(dst, src, flco=0, svc=0):
    return [flco, 0, svc, (dst >> 16) & 255, (dst >> 8) & 255, dst & 255, (src >> 16) & 255, (src >> 8) & 255, src & 255]

def full_lc(dst, src, dtype):
    d = lc_bytes(dst, src)
    mask = 0x96 if dtype == DT_VOICE_LC_HEADER else 0x99
    for j in range(3):
        v = 0
        for i in range(9): v ^= gf(d[i], RS[i][j])
        d.append(v ^ mask)
    return d

def bptc196(data12):
    bits = [b for byte in data12 for b in bits_of(byte, 8)]
    mat = [[0] * 15 for _ in range(13)]
    for i in range(96): mat[(i + 3) // 11][(i + 3) % 11] = bits[i]
    for r in range(9):
        p = 0
        for c in range(11):
            if mat[r][c]: p ^= H15[c]
        for j in range(4): mat[r][11 + j] = (p >> (3 - j)) & 1
    for c in range(15):
        p = 0
        for r in range(9):
            if mat[r][c]: p ^= H13[r]
        for j in range(4): mat[9 + j][c] = (p >> (3 - j)) & 1
    enc = [0] * 196
    for r in range(13):
        for c in range(15): enc[1 + r * 15 + c] = mat[r][c]
    tx = [0] * 196
    for i in range(196): tx[(i * 181) % 196] = enc[i]       # ETSI B.1.1: encoded index i -> transmitted index (i*181) mod 196
    return tx

def golay_bits(code):
    p = 0
    for j in range(8):
        if code & (0x80 >> j): p ^= GOLAY_P[j]
    return bits_of((code << 12) | p, 20)

def data_burst(dtype, cc, payload_bits196, sync='bs_data'):
    b = [0] * 264
    for i, v in enumerate(payload_bits196): b[i if i < 98 else i + 68] = v
    st = golay_bits((cc << 4) | dtype)
    b[98:108] = st[:10]; b[156:166] = st[10:]
    b[108:156] = bits_of(SYNC[sync], 48)
    return b

def qr16(value7):
    p = 0
    for i in range(7):
        if value7 & (0x40 >> i): p ^= QR_P[i]
    return bits_of((value7 << 9) | p, 16)

def embedded_lc_wire(dst, src):
    """128 transmitted bits of the embedded LC (ETSI TS 102 361-1 B.2.2.1): see components/orcsdr_dmr d1.cpp decode_embedded_lc."""
    lc = lc_bytes(dst, src)
    bits = [b for byte in lc for b in bits_of(byte, 8)]
    m = [[0] * 16 for _ in range(8)]
    n = 0
    for r in range(2):
        for c in range(11): m[r][c] = bits[n]; n += 1
    for r in range(2, 7):
        for c in range(10): m[r][c] = bits[n]; n += 1
    chk = sum(lc) % 31
    for r in range(2, 7): m[r][10] = (chk >> (6 - r)) & 1
    for r in range(7):
        p = 0
        for c in range(11):
            if m[r][c]: p ^= ROW16[c]
        for k in range(5): m[r][11 + k] = (p >> (4 - k)) & 1
    for c in range(16): m[7][c] = sum(m[r][c] for r in range(7)) & 1
    return [m[i % 8][i // 8] for i in range(128)]

def voice_burst(frames3, cc, stage, wire128):
    """stage 0 = burst A (voice SYNC), 1..5 = B..F (EMB + embedded fragment)."""
    payload = [b for f in frames3 for b in f]                  # 216 bits = 3 x 72
    b = [0] * 264
    b[0:108] = payload[:108]; b[156:264] = payload[108:]
    if stage == 0:
        b[108:156] = bits_of(SYNC['bs_voice'], 48)
    else:
        lcss = {1: 1, 2: 3, 3: 3, 4: 2, 5: 0}[stage]
        emb = qr16((cc << 3) | lcss)                           # CC(4) PI(1)=0 LCSS(2)
        frag = wire128[(stage - 1) * 32:(stage - 1) * 32 + 32] if stage <= 4 else [0] * 32
        b[108:116] = emb[:8]; b[116:148] = frag; b[148:156] = emb[8:]
    return b

def cach_bits(slot):
    p, q, r, s = 0, 1 if slot == 2 else 0, 0, 0               # AT=0, TC=slot-1, LCSS=00
    h, j, k = p ^ q ^ r, q ^ r ^ s, p ^ q ^ s
    out = [0] * 24
    for pos, v in zip((0, 4, 8, 12, 14, 18, 22), (p, q, r, s, h, j, k)): out[pos] = v
    return out

def read_frames(path):
    fr = []
    for line in open(path):
        h = line.split()[-1] if line.split() else ''
        if len(h) == 18: fr.append(bytes.fromhex(h))
    if not fr: raise SystemExit('no 18-hex-digit frames in ' + str(path))
    return fr

def build_call(frames, src, dst, cc, slot, superframes, lead_pairs=6, tail_pairs=6, header_repeats=3):
    """Returns (dibits list, label dict). Frames are consumed in order, 18 per superframe, looping."""
    idle = data_burst(DT_IDLE, cc, bptc196([0] * 12))
    hdr = data_burst(DT_VOICE_LC_HEADER, cc, bptc196(full_lc(dst, src, DT_VOICE_LC_HEADER)))
    term = data_burst(DT_VOICE_LC_HEADER + 1, cc, bptc196(full_lc(dst, src, DT_TERMINATOR_LC)))
    wire = embedded_lc_wire(dst, src)
    mine = [idle] * lead_pairs + [hdr] * header_repeats
    sent = []
    fi = 0
    for _ in range(superframes):
        for stage in range(6):
            trio = []
            for _ in range(3):
                f = frames[fi % len(frames)]; fi += 1
                sent.append(f.hex().upper())
                trio.append([x for byte in f for x in bits_of(byte, 8)])
            mine.append(voice_burst(trio, cc, stage, wire))
    mine += [term] + [idle] * tail_pairs
    # BS downlink: TS1 and TS2 alternate every 144 dibits (12 CACH + 132 burst). The call is on `slot`, the other slot is idle.
    dib = []
    for k, burst in enumerate(mine):
        for s in (1, 2):
            bb = burst if s == slot else idle
            bits = cach_bits(s) + bb
            dib += [(bits[2 * i] << 1) | bits[2 * i + 1] for i in range(len(bits) // 2)]
    label = dict(src=src, dst=dst, cc=cc, slot=slot, superframes=superframes, frames_sent=sent, header_repeats=header_repeats,
                 lead_pairs=lead_pairs, tail_pairs=tail_pairs, dibits=len(dib), seconds=len(dib) / 4800.0, voice_start_s=(lead_pairs + header_repeats) * 288 / 4800.0)
    return dib, label

LEVEL = {0: 1, 1: 3, 2: -1, 3: -3}

def pulse(t, b=0.2):
    t = np.asarray(t, dtype=float)
    out = np.empty_like(t)
    for i, x in enumerate(t.flat):
        if abs(x) < 1e-10: v = 1 + b * (4 / np.pi - 1)
        elif abs(abs(x) - 1 / (4 * b)) < 1e-9:
            v = b / np.sqrt(2) * ((1 + 2 / np.pi) * np.sin(np.pi / (4 * b)) + (1 - 2 / np.pi) * np.cos(np.pi / (4 * b)))
        else: v = (np.sin(np.pi * (1 - b) * x) + 4 * b * x * np.cos(np.pi * (1 + b) * x)) / (np.pi * x * (1 - 16 * b * b * x * x))
        out.flat[i] = v
    return out

def modulate48(dibits, lead=130, tail=110):
    """4FSK at 48 kS/s (10 samples per symbol), complex baseband, same pulse shaping as the receiver's test synthesizer."""
    taps = pulse(np.arange(-30, 31) / 10.0)
    taps = taps * (10.0 / taps.sum()) * 648.0
    imp = np.zeros(lead + len(dibits) * 10 + tail)
    for i, d in enumerate(dibits): imp[lead + i * 10] = LEVEL[int(d)]
    freq = np.convolve(imp, taps, mode='same')
    phase = np.cumsum(2 * np.pi * freq / 48000.0)
    return np.exp(1j * phase).astype(np.complex64)

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--frames', required=True); ap.add_argument('--src', type=int, required=True); ap.add_argument('--dst', type=int, required=True)
    ap.add_argument('--cc', type=int, default=1); ap.add_argument('--slot', type=int, choices=(1, 2), default=1)
    ap.add_argument('--superframes', type=int, default=8); ap.add_argument('--out', required=True)
    a = ap.parse_args()
    dib, label = build_call(read_frames(a.frames), a.src, a.dst, a.cc, a.slot, a.superframes)
    bb = modulate48(dib)
    label['baseband_samples'] = len(bb); label['baseband_rate'] = 48000
    label['baseband_sha256'] = hashlib.sha256(bb.tobytes()).hexdigest()
    np.savez(a.out, baseband=bb, label=json.dumps(label))
    print(json.dumps({k: v for k, v in label.items() if k != 'frames_sent'}, indent=1))

if __name__ == '__main__':
    main()
