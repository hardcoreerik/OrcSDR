#!/usr/bin/env python3
"""Record one real Meshtastic packet from the Tab5 as raw CU8 IQ for sensitivity tests.

Starts a manual 4 s LoRa capture on OrcSDR, sends one text from a Meshtastic
node about one second in, and retrieves the IQ over serial. The Meshtastic
node's configuration is not changed.
"""
from __future__ import annotations

import argparse
import sys
import threading
import time
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
import decode_orciq  # noqa: E402
from help_media import Tab5  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="ORCIQ capture file to write (36-byte header + 960 kS/s CU8)")
    parser.add_argument("--orcsdr-port", default="COM17")
    parser.add_argument("--tx-port", required=True, help="Meshtastic node that transmits")
    parser.add_argument("--text", default="OrcSDR sweep")
    parser.add_argument("--pairing-key", type=Path,
                        default=TOOLS.parents[0] / ".orclink" / "ui-doc.key")
    args = parser.parse_args()

    import meshtastic.serial_interface as serial_interface
    node = serial_interface.SerialInterface(args.tx_port)
    tab5 = None
    timer = None
    try:
        time.sleep(4)
        tab5 = Tab5(args.orcsdr_port, args.pairing_key)
        tab5.authenticate()
        tab5.send("RTL_IQ_START")
        started = tab5.wait(("RTL_IQ_START", "RTL_IQ_ERROR"), 10)
        if started.startswith("RTL_IQ_ERROR"):
            raise RuntimeError(started)
        timer = threading.Timer(1.0, lambda: node.sendText(args.text))
        timer.start()
        tab5.wait(("RTL_IQ_DONE",), 20)
        timer.join()
        tab5.wait(("RTL_LORA_NATIVE_DONE",), 60)
        state, capture = decode_orciq._retrieve_latest_iq(tab5.serial)
        args.output.write_bytes(capture)
        # Release the retained manual capture; while it is held, live automatic
        # detection is blocked and a new manual capture is refused.
        tab5.send("RTL_IQ_RETRIEVE_END")
        print(f"CAPTURED bytes={len(capture)} state={state}")
    finally:
        if timer is not None:
            timer.cancel()
        if tab5 is not None:
            tab5.close()
        node.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
