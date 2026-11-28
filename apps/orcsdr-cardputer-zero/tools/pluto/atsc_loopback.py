#!/usr/bin/env python3
"""ATSC loopback, mirroring upstream gr-dtv's file_atsc_tx.grc transmitter.

The transmit chain is the reference one: atsc_pad, randomizer, RS encoder,
interleaver, trellis encoder, field sync mux, keep_m_in_n, 8-VSB modulator, pilot
rotator and root-raised-cosine pulse shaping.

Measured behaviour, recorded because it is the only evidence this receiver has ever
produced:

  with the rotator    -> 135360 bytes, 720 packets, all framed 0x47, payload wrong
  without the rotator -> 0 bytes, no lock at all

So the pilot offset is required for lock, and the fault lies downstream of it: the
payload is garbage while the framing is perfect, which points at the trellis decoder
or the post-equalizer phase ambiguity rather than at sync acquisition.

Run: atsc_loopback.py [packets]
"""
import math
import os
import sys

from gnuradio import blocks, dtv, gr
from gnuradio import filter as grfilter
from gnuradio.filter import firdes

SYMBOL_RATE = 4500000.0 / 286.0 * 684.0     # 10.762237 MHz
PILOT_FREQ = (6000000.0 - (SYMBOL_RATE / 2.0)) / 2.0
TX_TS = "/tmp/atsc_tx_in.ts"
RX_TS = "/tmp/atsc_rx_out.ts"
PACKETS = int(sys.argv[1]) if len(sys.argv) > 1 else 2000
ITEM = 188


def make_transport_stream(path, packets):
    """A byte stream of 188-byte MPEG packets; atsc_pad does the alignment."""
    with open(path, "wb") as handle:
        for n in range(packets):
            packet = bytearray(ITEM)
            packet[0] = 0x47
            packet[1] = 0x20 if n % 2 else 0x00
            packet[2] = n & 0xFF
            packet[3] = 0x10
            for i in range(4, 188):
                packet[i] = (n + i) & 0xFF
            handle.write(packet)


def main():
    make_transport_stream(TX_TS, PACKETS)
    if os.path.exists(RX_TS):
        os.remove(RX_TS)

    tb = gr.top_block("atsc_loopback")
    src = blocks.file_source(gr.sizeof_char, TX_TS, False)

    pad = dtv.atsc_pad()
    randomizer = dtv.atsc_randomizer()
    rs_encoder = dtv.atsc_rs_encoder()
    interleaver = dtv.atsc_interleaver()
    trellis = dtv.atsc_trellis_encoder()
    fs_mux = dtv.atsc_field_sync_mux()
    v2s = blocks.vector_to_stream(gr.sizeof_char, 1024)
    keep = blocks.keep_m_in_n(gr.sizeof_char, 832, 1024, 4)
    modulator = dtv.dvbs2_modulator_bc(
        dtv.FECFRAME_NORMAL, dtv.C1_4, dtv.MOD_8VSB, dtv.INTERPOLATION_OFF)
    # Pilot offset, exactly as the reference transmitter computes it. Required:
    # without it the receiver does not lock at all.
    rotator = blocks.rotator_cc(((-3000000.0 + PILOT_FREQ) / SYMBOL_RATE) * (math.pi * 2.0))
    shaping = grfilter.fft_filter_ccc(
        1, firdes.root_raised_cosine(0.11, SYMBOL_RATE, SYMBOL_RATE / 2.0, 0.1152, 200))

    upsample = grfilter.rational_resampler_ccf(2, 1)
    rx = dtv.atsc_rx(SYMBOL_RATE * 2.0, 2.0)
    sink = blocks.file_sink(1, RX_TS, False)

    tb.connect(src, pad, randomizer, rs_encoder, interleaver, trellis, fs_mux,
               v2s, keep, modulator, rotator, shaping)
    tb.connect(shaping, upsample, rx, sink)

    print("loopback: %d packets, %.0f sym/s, pilot %.0f Hz" % (PACKETS, SYMBOL_RATE, PILOT_FREQ),
          flush=True)
    tb.run()
    size = os.path.getsize(RX_TS) if os.path.exists(RX_TS) else 0
    print("done, %d bytes of transport stream" % size, flush=True)


if __name__ == "__main__":
    main()
