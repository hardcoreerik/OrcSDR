#!/usr/bin/env python3
"""Set a Tab5 hardware clock from this computer without internet access."""

import argparse
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from help_media import Tab5  # noqa: E402


MIN_UTC = 1_704_067_200  # 2024-01-01
MAX_UTC = 4_102_444_800  # 2100-01-01, outside the Tab5 RTC range


def rtc_set_command(epoch: int) -> str:
    if not MIN_UTC <= epoch < MAX_UTC:
        raise ValueError("UTC time is outside the Tab5 RTC range")
    return f"ORC_RTC_SET {epoch}"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="Tab5 USB serial port, for example COM17")
    parser.add_argument("--pairing-key", type=Path, default=Path(".orclink/ui-doc.key"))
    args = parser.parse_args()

    tab5 = Tab5(args.port, args.pairing_key)
    try:
        tab5.authenticate()
        tab5.send(rtc_set_command(int(time.time())))
        reply = tab5.wait(("ORC_RTC_SET_OK", "ORC_RTC_SET_ERROR"))
        if not reply.startswith("ORC_RTC_SET_OK"):
            raise RuntimeError(reply)
        print(reply)
    finally:
        tab5.close()


if __name__ == "__main__":
    main()
