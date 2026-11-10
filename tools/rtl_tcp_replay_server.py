#!/usr/bin/env python3
"""A fake rtl_tcp server that replays a recorded audio WAV as narrow-FM IQ, for proving a decoder over the rtl_tcp path.

  python tools/rtl_tcp_replay_server.py --bind 192.168.1.15 --port 1235 robot_36.wav

It speaks the real protocol: a 12-byte greeting (RTL0, tuner type, gain count), 5-byte big-endian commands in
(the rate command sets the stream rate; the rest are accepted and ignored, there is no acknowledgement), raw
8-bit unsigned interleaved IQ out, paced at the real sample rate. The WAV (any rate, mono PCM16) is
frequency-modulated onto the carrier at --deviation Hz and repeated with --gap seconds of noise between plays.
The carrier sits at the centre of the IQ, so the receiver must be tuned to a narrow-FM mode; the tuned frequency
is ignored. Pair it with `SSTV TUNE 145500000` on the Tab5 (narrow FM) and `RTL_TCP HOST <this PC> <port>`.

Safety: rtl_tcp has no authentication, so this refuses to bind to 0.0.0.0 or ::; name one interface address.
One client at a time, like a real rtl_tcp (a second client waits). Needs numpy and scipy.
"""
import argparse
import math
import socket
import struct
import sys
import threading
import time
import wave
from fractions import Fraction

import numpy as np
from scipy.signal import resample_poly


def read_wav(path):
    with wave.open(path, "rb") as w:
        if w.getnchannels() != 1 or w.getsampwidth() != 2:
            sys.exit("the WAV must be mono PCM16")
        rate = w.getframerate()
        data = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float64) / 32768.0
    return rate, data


def fm_iq(audio, audio_rate, out_rate, deviation, level, noise, rng):
    """Narrow-FM modulate `audio` and return interleaved CU8 bytes at `out_rate`."""
    ratio = Fraction(out_rate, audio_rate).limit_denominator(1000)
    up = resample_poly(audio, ratio.numerator, ratio.denominator)
    phase = 2.0 * math.pi * deviation * np.cumsum(up) / out_rate
    iq = level * np.exp(1j * phase)
    if noise > 0:
        iq = iq + noise * (rng.standard_normal(iq.size) + 1j * rng.standard_normal(iq.size))
    return to_cu8(iq)


def noise_iq(seconds, out_rate, level, noise, rng):
    n = int(seconds * out_rate)
    iq = noise * (rng.standard_normal(n) + 1j * rng.standard_normal(n))
    return to_cu8(iq if noise > 0 else np.zeros(n, dtype=complex))


def to_cu8(iq):
    out = np.empty(iq.size * 2, dtype=np.uint8)
    out[0::2] = np.clip(np.rint(127.5 + 127.0 * iq.real), 0, 255)
    out[1::2] = np.clip(np.rint(127.5 + 127.0 * iq.imag), 0, 255)
    return out.tobytes()


class Client:
    def __init__(self, conn, addr, args, audio_rate, audio):
        self.conn, self.addr, self.args = conn, addr, args
        self.audio_rate, self.audio = audio_rate, audio
        self.rate = 240000
        self.rate_changed = threading.Event()
        self.alive = True
        self.commands = []

    def reader(self):
        buf = b""
        try:
            while self.alive:
                chunk = self.conn.recv(4096)
                if not chunk:
                    break
                buf += chunk
                while len(buf) >= 5:
                    op, val = buf[0], struct.unpack(">I", buf[1:5])[0]
                    buf = buf[5:]
                    self.commands.append((op, val))
                    if op == 2 and val != self.rate:
                        self.rate = val
                        self.rate_changed.set()
                    print(f"  command 0x{op:02x} {val}", flush=True)
        except OSError:
            pass
        self.alive = False

    def run(self):
        a = self.args
        self.conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.conn.sendall(b"RTL0" + struct.pack(">II", a.tuner, a.gains))
        threading.Thread(target=self.reader, daemon=True).start()
        rng = np.random.default_rng(1)
        time.sleep(0.4)   # let the client's rate command arrive before the first block is built
        while self.alive:
            rate = self.rate
            self.rate_changed.clear()
            tx = fm_iq(self.audio, self.audio_rate, rate, a.deviation, a.level, a.noise, rng)
            gap = noise_iq(a.gap, rate, a.level, a.noise, rng)
            seconds = len(tx) / 2 / rate
            print(f"{self.addr[0]}: replaying {seconds:.1f} s at {rate} S/s (+{a.gap:.0f} s gap)", flush=True)
            for stream in (gap, tx, gap):
                if not self.pace(stream, rate): break
            if not self.alive: break
            if a.once: break
        self.alive = False

    def pace(self, data, rate):
        block = 4096                      # bytes = 2048 samples
        per_block = (block / 2) / rate    # seconds of signal per block
        start, sent = time.perf_counter(), 0
        for i in range(0, len(data), block):
            if not self.alive or self.rate_changed.is_set():
                return False   # a new rate: the caller rebuilds the stream; a dead client ends the loop
            try:
                self.conn.sendall(data[i:i + block])
            except OSError:
                self.alive = False
                return False
            sent += 1
            wait = start + sent * per_block - time.perf_counter()
            if wait > 0:
                time.sleep(wait)
        return True


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("wav")
    p.add_argument("--bind", required=True, help="an IPv4 address of this machine (never 0.0.0.0)")
    p.add_argument("--port", type=int, default=1235)
    p.add_argument("--deviation", type=float, default=2500.0, help="FM deviation in Hz at full-scale audio")
    p.add_argument("--level", type=float, default=0.5, help="carrier amplitude, 0..1 of full scale")
    p.add_argument("--noise", type=float, default=0.01, help="complex noise sigma, same scale as --level")
    p.add_argument("--gap", type=float, default=4.0, help="seconds of noise before, between and after plays")
    p.add_argument("--tuner", type=int, default=5, help="tuner type in the greeting (5 = R820T)")
    p.add_argument("--gains", type=int, default=29, help="gain-step count in the greeting")
    p.add_argument("--once", action="store_true", help="play once per client instead of looping")
    args = p.parse_args()
    if args.bind in ("0.0.0.0", "::", ""):
        sys.exit("refusing to bind to all interfaces: rtl_tcp has no authentication; name one address")
    audio_rate, audio = read_wav(args.wav)
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((args.bind, args.port))
    srv.listen(1)
    print(f"rtl_tcp replay server on {args.bind}:{args.port}, {len(audio)/audio_rate:.1f} s of audio from {args.wav}", flush=True)
    try:
        while True:
            conn, addr = srv.accept()
            print(f"client {addr[0]}:{addr[1]} connected", flush=True)
            Client(conn, addr, args, audio_rate, audio).run()
            conn.close()
            print("client gone", flush=True)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
