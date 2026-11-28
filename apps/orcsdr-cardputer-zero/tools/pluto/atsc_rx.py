#!/usr/bin/env python3
"""ATSC 1.0 receiver on the Pluto's own Linux.

Runs entirely on the Zynq's ARM cores: the AD9361 is sampled at 6.5 MS/s locally,
so no IQ crosses the USB link - only the recovered transport stream would. This
exists because the CardPuter Zero cannot sustain the IQ rate (measured: 7.4 MB/s
against the 13 MB/s that a 6 MHz channel needs), while the Pluto can capture at
that rate without moving the data anywhere.

Usage: atsc_rx.py [frequency_hz] [seconds]
"""
import sys
import time

from gnuradio import blocks, dtv, gr, iio

FREQ = float(sys.argv[1]) if len(sys.argv) > 1 else 557e6   # KVAL RF 28
SECONDS = float(sys.argv[2]) if len(sys.argv) > 2 else 10.0
RATE = int(sys.argv[3]) if len(sys.argv) > 3 else 10762238
TS_PATH = "/tmp/atsc_ts.bin"


def call(obj, name, *args):
    """gr-iio's API differs across versions; a missing method must not stop us."""
    fn = getattr(obj, name, None)
    if fn is None:
        sys.stderr.write("%s unavailable\n" % name)
        return
    try:
        fn(*args)
    except Exception as exc:
        sys.stderr.write("%s failed: %s\n" % (name, exc))


def main():
    tb = gr.top_block("atsc_rx")

    # The Pluto is the device, so a local context is enough.
    src = iio.fmcomms2_source_fc32("local:", [True, True], 32768)
    call(src, "set_len_tag", 0)
    call(src, "set_frequency", FREQ)
    call(src, "set_samplerate", int(RATE))
    call(src, "set_bandwidth", int(RATE))
    # Manual gain keeps the measurement repeatable; AGC hides weak signals.
    call(src, "set_gain_mode", 0, "manual")
    call(src, "set_gain", 0, 60)

    # ATSC symbol rate is 10.762237 Msym/s, so sps follows from the chosen input\n    # rate; a 6 MHz channel needs at least ~6.5 MS/s.
    # gr-iio has no usable set_bandwidth in this version, but the analogue filter is
    # worth about 7 dB of SNR: at 10.76 MHz wide the 6 MHz channel was being received
    # through a filter twice its width. Set it directly, after gr-iio has finished
    # configuring, and before streaming starts.
    import subprocess
    subprocess.run(["iio_attr", "-u", "local:", "-c", "ad9361-phy", "voltage0",
                    "rf_bandwidth", "5000000"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
    rx = dtv.atsc_rx(RATE, RATE / 10762238.0)
    sink = blocks.file_sink(gr.sizeof_char, TS_PATH, False)

    tb.connect(src, rx)
    tb.connect((rx, 0), sink)

    print("tuning %.3f MHz, %.3f MS/s, %.1f s" % (FREQ / 1e6, RATE / 1e6, SECONDS), flush=True)
    tb.start()
    time.sleep(SECONDS)
    tb.stop()
    tb.wait()
    print("done", flush=True)


if __name__ == "__main__":
    main()
