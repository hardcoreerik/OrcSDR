#!/usr/bin/env python3
"""Capture a dongle's IQ on a Raspberry Pi and decode FT8 on the Pi itself with the production decoder.

Runs ON the Pi (needs numpy, the Blog rtl_sdr, and the decoder built from the repo's tools/ft8-wav-native.cpp). It answers
one question without rtl_tcp, the network or a Tab5 in the way: does this dongle and antenna, in this place, receive FT8?

  python3 pi_ft8_capture_decode.py --freq 7074000 --slots 8 --gain 40 --decoder /tmp/ft8build/ft8-wav-native

It stops the rtl-tcp service (the dongle is exclusive) and starts it again at the end. The IQ (240 kS/s) is turned into
upper-sideband audio at 12 kS/s by keeping the positive-frequency half of each 15 s window's spectrum (what the Tab5's audio
tap does), cut at UTC 15 s boundaries using the time the first sample arrived (the Pi's clock must be NTP-synced), written as a
WAV and decoded. Prints one line per slot and a total. DT values read a little high: the capture start is only known to about
0.1 s. Receive only; nothing is transmitted.
"""
import argparse
import re
import subprocess
import sys
import time
import wave

import numpy as np

RATE = 240000
SLOT = 15


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--freq", type=int, default=7074000, help="dial frequency in Hz")
    p.add_argument("--slots", type=int, default=8)
    p.add_argument("--gain", type=float, default=40.0, help="tuner gain in dB (manual)")
    p.add_argument("--decoder", default="/tmp/ft8build/ft8-wav-native")
    p.add_argument("--no-service", action="store_true", help="do not stop/start the rtl-tcp service")
    p.add_argument("--keep", default="", help="directory to keep the slot WAVs in")
    a = p.parse_args()

    if not a.no_service:
        subprocess.run(["sudo", "-n", "systemctl", "stop", "rtl-tcp"], check=False)
        time.sleep(2)
    try:
        total_s = a.slots * SLOT + 2 * SLOT                      # room to drop the start-up and to align to a boundary
        n_samples = total_s * RATE
        proc = subprocess.Popen(["/opt/rtlsdr-blog/bin/rtl_sdr", "-f", str(a.freq), "-s", str(RATE), "-g", str(a.gain), "-n", str(n_samples), "-"],
                                stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, bufsize=0)
        buf = bytearray()
        t_first = None
        want = n_samples * 2
        while len(buf) < want:
            chunk = proc.stdout.read(262144)
            if not chunk:
                break
            if t_first is None:
                t_first = time.time()
            buf += chunk
        proc.wait(timeout=10)
        if t_first is None or len(buf) < RATE * 2 * 20:
            sys.exit("no samples captured: is the dongle free and plugged in?")
        t0 = t_first - 262144 / 2 / RATE                          # the first chunk arrived after one buffer of samples
        raw = np.frombuffer(bytes(buf), dtype=np.uint8)
        iq = ((raw[0::2].astype(np.float32) - 127.5) + 1j * (raw[1::2].astype(np.float32) - 127.5)) / 127.5
        print(f"captured {len(iq)/RATE:.1f} s at {RATE} S/s, first sample at UTC {t0:.2f}, IQ rms {20*np.log10(np.sqrt(np.mean(np.abs(iq)**2))):.1f} dBFS", flush=True)

        first_boundary = int(np.ceil((t0 + 1.5) / SLOT)) * SLOT
        results = []
        for k in range(a.slots):
            boundary = first_boundary + k * SLOT
            start = int(round((boundary - t0) * RATE))
            end = start + SLOT * RATE
            if end > len(iq):
                break
            x = np.fft.fft(iq[start:end])
            n_audio = SLOT * 12000
            half = n_audio // 2 + 1                                # bins 0 .. 6 kHz, 1/15 Hz apart in both spectra
            audio = np.fft.irfft(x[:half], n=n_audio)             # upper sideband: keep the positive frequencies only
            audio *= 1000.0 / max(np.sqrt(np.mean(audio ** 2)), 1e-9)
            pcm = np.clip(np.rint(audio), -32767, 32767).astype("<i2")
            path = f"{a.keep or '/tmp'}/pi_slot_{k}.wav"
            with wave.open(path, "wb") as w:
                w.setnchannels(1); w.setsampwidth(2); w.setframerate(12000); w.writeframes(pcm.tobytes())
            out = subprocess.run([a.decoder, path, "ft8"], capture_output=True, text=True).stdout
            rows = re.findall(r"DECODE (.+?)\s+(\d+) Hz\s+dt\s+([+-]\d+) ms\s+snr\s+(-?\d+)", out)
            results.append((boundary, rows))
            print(f"slot {time.strftime('%H:%M:%S', time.gmtime(boundary))} UTC: {len(rows)} decoded" +
                  ("  " + "; ".join(f"{m.strip()} {hz}Hz {snr}dB" for m, hz, dt, snr in rows[:6]) if rows else ""), flush=True)
        n = sum(len(r) for _, r in results)
        print(f"TOTAL {n} decodes in {len(results)} slots, mean {n/max(len(results),1):.1f} per slot, distinct messages {len({m for _, r in results for m, *_ in r})}")
    finally:
        if not a.no_service:
            subprocess.run(["sudo", "-n", "systemctl", "start", "rtl-tcp"], check=False)


if __name__ == "__main__":
    main()
