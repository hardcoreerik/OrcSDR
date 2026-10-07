#!/usr/bin/env python3
"""Set a Tab5 hardware clock from this computer without internet access."""

import argparse
import re
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


MIN_OFFSET_MINUTES = -12 * 60
MAX_OFFSET_MINUTES = 14 * 60


def tz_set_command(minutes: int) -> str:
    if not MIN_OFFSET_MINUTES <= minutes <= MAX_OFFSET_MINUTES:
        raise ValueError("UTC offset must be between -720 and 840 minutes")
    return f"ORC_TZ_SET {minutes}"


def status_offset_minutes(status_line: str):
    """The offset_min value in an ORC_RTC_STATUS line, or None when absent."""
    match = re.search(r"offset_min=(-?\d+)", status_line)
    return int(match.group(1)) if match else None


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="Tab5 USB serial port, for example COM17")
    parser.add_argument("--utc-offset-minutes", type=int,
                        help="also store the local UTC offset in minutes east of UTC, e.g. -420 for UTC-07:00")
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
        if args.utc_offset_minutes is not None:
            tab5.send(tz_set_command(args.utc_offset_minutes))
            reply = tab5.wait(("ORC_TZ_SET_OK", "ORC_TZ_SET_ERROR"))
            if not reply.startswith("ORC_TZ_SET_OK"):
                raise RuntimeError(reply)
            print(reply)
    finally:
        tab5.close()


if __name__ == "__main__":
    main()
