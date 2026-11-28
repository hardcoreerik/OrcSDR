#!/usr/bin/env python3
"""ATSC loopback: gr-dtv's transmitter feeding gr-dtv's receiver.

Configuration mirrors upstream's own example (gr-dtv/examples/file_atsc_rx.grc):
input 6.25 MS/s with sps 1.5. Earlier attempts here used 6.5 MS/s with sps 1.0 and
2.0, which are not configurations the reference uses, and none of them locked.

Run: atsc_loopback.py [packets]
"""
import os
import sys

from gnuradio import blocks, dtv, gr
from gnuradio import filter as grfilter

SYMBOL_RATE = 4.5e6 / 286.0 * 684.0      # 10.762237 MHz, ATSC 1.0
INPUT_RATE = 6.25e6                      # upstream example's rate
RX_SPS = 1.5                             # upstream example's sps
TX_TS = "/tmp/atsc_tx_in.ts"
RX_TS = "/tmp/atsc_rx_out.ts"
PACKETS = int(sys.argv[1]) if len(sys.argv) > 1 else 2000
ITEM = 256


def make_transport_stream(path, packets):
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
    src = blocks.file_source(ITEM, TX_TS, False)

    # ATSC order: randomise, RS encode, interleave, then trellis.
    randomizer = dtv.atsc_randomizer()
    rs_encoder = dtv.atsc_rs_encoder()
    interleaver = dtv.atsc_interleaver()
    trellis = dtv.atsc_trellis_encoder()
    fs_mux = dtv.atsc_field_sync_mux()
    unpack = blocks.vector_to_stream(gr.sizeof_gr_complex, 128)

    # Resample the symbol-rate stream to the 6.25 MS/s the reference expects.
    # 625/1076 approximates 6.25e6 / 10.762237e6.
    resample = grfilter.rational_resampler_ccf(625, 1076)
    pilot = blocks.add_const_cc(0.2)

    rx = dtv.atsc_rx(INPUT_RATE, RX_SPS)
    sink = blocks.file_sink(1, RX_TS, False)

    tb.connect(src, randomizer, rs_encoder, interleaver, trellis, fs_mux,
               unpack, resample, pilot)
    tb.connect(pilot, rx, sink)

    print("loopback: %d packets, %.0f sample/s, sps %.1f" % (PACKETS, INPUT_RATE, RX_SPS),
          flush=True)
    tb.run()
    print("done", flush=True)


if __name__ == "__main__":
    main()
