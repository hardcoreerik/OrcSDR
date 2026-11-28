#!/usr/bin/env python3
"""ATSC loopback: gr-dtv's transmitter chain feeding gr-dtv's receiver chain.

Exists to separate two questions that "0 bytes of transport stream" cannot
distinguish: is the decoder wrong, or is there simply no signal? Both ends are the
same code base, so a clean recovery here proves the receive chain decodes, and any
later failure on air is then genuinely an antenna or signal problem.

Run: atsc_loopback.py [packets]
"""
import os
import sys

from gnuradio import blocks, dtv, gr

SYMBOL_RATE = 10762237.0       # ATSC 1.0 symbol rate; sps = 1 at this rate
TX_TS = "/tmp/atsc_tx_in.ts"
RX_TS = "/tmp/atsc_rx_out.ts"
PACKETS = int(sys.argv[1]) if len(sys.argv) > 1 else 2000
ITEM = 256                     # gr-dtv passes MPEG packets as 256-byte items


def make_transport_stream(path, packets):
    """MPEG-TS in 256-byte items: a 188-byte packet plus padding."""
    with open(path, "wb") as handle:
        for n in range(packets):
            packet = bytearray(ITEM)
            packet[0] = 0x47
            packet[1] = 0x20 if n % 2 else 0x00      # PID 0x0020
            packet[2] = n & 0xFF
            packet[3] = 0x10                          # payload only
            for i in range(4, 188):
                packet[i] = (n + i) & 0xFF
            handle.write(packet)


def main():
    make_transport_stream(TX_TS, PACKETS)
    if os.path.exists(RX_TS):
        os.remove(RX_TS)

    tb = gr.top_block("atsc_loopback")
    src = blocks.file_source(ITEM, TX_TS, False)

    # Standard ATSC order: randomise, RS encode, then interleave *before* trellis
    # coding. Trellis-then-interleave is a plausible-looking mistake.
    randomizer = dtv.atsc_randomizer()
    rs_encoder = dtv.atsc_rs_encoder()
    interleaver = dtv.atsc_interleaver()
    trellis = dtv.atsc_trellis_encoder()
    fs_mux = dtv.atsc_field_sync_mux()

    # field_sync_mux emits fixed vectors of complex samples; atsc_rx consumes a
    # plain sample stream, so unpack them. 1024 bytes = 128 complex64.
    unpack = blocks.vector_to_stream(gr.sizeof_gr_complex, 128)

    rx = dtv.atsc_rx(SYMBOL_RATE, 1.0)
    # atsc_rx emits a byte stream (itemsize 1), not whole packets.
    sink = blocks.file_sink(1, RX_TS, False)

    tb.connect(src, randomizer, rs_encoder, interleaver, trellis, fs_mux)
    tb.connect(fs_mux, unpack, rx, sink)

    print("loopback: %d packets, %.0f sym/s, sps 1.0" % (PACKETS, SYMBOL_RATE), flush=True)
    tb.run()
    print("done", flush=True)


if __name__ == "__main__":
    main()
