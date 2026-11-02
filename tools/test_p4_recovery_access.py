"""Hardware check of Hosted/ROM SDIO handoff; never connects ROM or writes C6.

Explicit run temporarily stops Wi-Fi, then always requests its resumption.
Receiver streaming must continue. Uses existing pairing/authentication.
"""
import argparse
import json
from pathlib import Path
import re
import time
import tab5_ft8 as transport


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--port", required=True)
    p.add_argument("--baud", type=int, default=921600)
    p.add_argument("--key")
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    dev = transport.Tab5(args.port, args.baud)
    key = transport.find_key(args.key)
    report = {}
    requested = False
    try:
        dev.authenticate(key)

        def query(command, prefix):
            dev.send(command)
            lines = dev.read_until((prefix,), 4, echo=False)
            line = next((line for line in lines if prefix in line), None)
            if line is None:
                raise RuntimeError("No response to " + command)
            return dict(re.findall(r"(\w+)=([^\s]+)", line))

        def wait_access(state, timeout=40):
            until = time.monotonic() + timeout
            while time.monotonic() < until:
                status = query("RTL_C6_ROM STATUS", "RTL_C6_ROM_STATUS")
                if status.get("access") == state:
                    return status
                if status.get("access") == "3":
                    raise RuntimeError("Hosted teardown failed")
                time.sleep(.25)
            raise RuntimeError("Timed out waiting for recovery access " + state)

        before = query("RTL_STATUS", "RTL_SDR_STATUS")
        if before.get("streaming") != "1":
            raise RuntimeError("Receiver is not streaming before handoff")
        report["before"] = before
        status = wait_access("0", 5)
        if status.get("embedded") != "1" or status.get("write_enabled") != "0":
            raise RuntimeError("Expected embedded package with ROM writes disabled")
        requested = True
        dev.send("RTL_C6_ROM QUIESCE")
        reply = dev.read_until(("RTL_C6_ROM_QUIESCE_QUEUED", "RTL_C6_ROM_ERROR"), 4, echo=False)
        if not any("RTL_C6_ROM_QUIESCE_QUEUED" in line for line in reply):
            raise RuntimeError("Recovery request rejected")
        report["quiesced"] = wait_access("2")
        during = query("RTL_STATUS", "RTL_SDR_STATUS")
        if during.get("streaming") != "1" or during.get("frequency_hz") != before.get("frequency_hz"):
            raise RuntimeError("Receiver changed during handoff")
        report["during"] = during
        dev.authenticate(key)
        dev.send("RTL_C6_ROM RESUME")
        dev.read_until(("RTL_C6_ROM_RESUME_QUEUED",), 4, echo=False)
        report["resumed"] = wait_access("0")
        requested = False
        until = time.monotonic() + 30
        while time.monotonic() < until:
            wifi = query("RTL_WIFI STATUS", "RTL_WIFI_STATUS")
            if wifi.get("started") == "1":
                break
            time.sleep(.5)
        else:
            raise RuntimeError("Wi-Fi did not restart after handoff")
        # Avoid writing SSIDs/IPs/credentials into the hardware report.
        report["wifi_restarted"] = True
        report["after"] = query("RTL_STATUS", "RTL_SDR_STATUS")
        if report["after"].get("streaming") != "1":
            raise RuntimeError("Receiver stopped after handoff")
        report["result"] = "PASS"
        print("PASS: Hosted released/resumed SDIO; radio stayed streaming; no C6 write")
    finally:
        if requested:
            try:
                dev.authenticate(key)
                dev.send("RTL_C6_ROM RESUME")
                dev.read_until(("RTL_C6_ROM_RESUME_QUEUED",), 4, echo=False)
            except BaseException:
                report["cleanup"] = "Resume not confirmed; inspect device before ROM use"
        dev.s.close()
        args.output.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
