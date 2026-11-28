#!/usr/bin/env python3
"""Decode reference ATSC IQ produced by a third-party verified modulator.

The IQ comes from muxwright's atsc_mod (MIT), which documents itself as bit-exact
against gr-dtv and known to loop back through gr-dtv atsc_rx without packet errors.
That makes it ground truth for this project: it separates "our receiver is wrong"
from "our transmitter is wrong", a distinction nine rounds of self-built signals
could not make.

IQ format: int16 LE interleaved I/Q at 10,762,238 S/s, 1 sample per symbol.

Run: decode_ref.py [iq_path]
"""
import os
import sys

from gnuradio import blocks, dtv, gr

SYMBOL_RATE = 4500000.0 / 286.0 * 684.0     # 10.762238 MHz
IQ = sys.argv[1] if len(sys.argv) > 1 else "/tmp/ref_atsc.iq"
TS = "/tmp/ref_atsc.ts"


def main():
    if os.path.exists(TS):
        os.remove(TS)
    tb = gr.top_block("decode_ref")
    src = blocks.file_source(gr.sizeof_short, IQ, False)
    to_complex = blocks.interleaved_short_to_complex(False, False)
    rx = dtv.atsc_rx(SYMBOL_RATE, 1.0)
    sink = blocks.file_sink(1, TS, False)
    tb.connect(src, to_complex, rx, sink)

    print("decoding %s at %.0f S/s, sps 1.0" % (IQ, SYMBOL_RATE), flush=True)
    tb.run()
    size = os.path.getsize(TS) if os.path.exists(TS) else 0
    print("done, %d bytes of transport stream" % size, flush=True)


if __name__ == "__main__":
    main()
