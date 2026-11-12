#!/usr/bin/env python3
"""Repeatable AIS bench test on a Tab5: replay synthetic IQ over rtl_tcp, compare the device with the host decoder, and
screenshot every AIS page.

  1. python tools/ais_make_iq.py --out ais_fleet.cu8
  2. build tools/ais_decode_cu8.cpp and run it:   ais_decode_cu8 ais_fleet.cu8 > host_decode.txt
  3. python tools/rtl_tcp_replay_server.py --bind <this PC's LAN address> --port 1235 --iq ais_fleet.cu8 --rate 240000 --gap 1
  4. python tools/ais_tab5_replay_test.py --port COM17 --server <this PC's LAN address>:1235 \\
         --manifest ais_fleet.cu8.json --host host_decode.txt --out evidence_dir

The script points the Tab5's rtl_tcp source at the replay server, opens the AIS dashboard, marks the session as a test
replay (every page then says so), waits for --passes passes of the file, records RTL_AIS STATUS / RTL_HEALTH / RTL_TCP
STATUS, screenshots the seven pages, runs tools/ais_check_replay.py --device, then RESTORES the Tab5's previous rtl_tcp
server and source and returns to Home. It reboots nothing and flashes nothing.

Network replay only: the replay server is a private TCP substitute for an rtl_tcp server. Nothing here transmits RF.
The pairing key is read from the usual key file (see tools/tab5_ft8.py); no address or key is written to the output
except the server address you pass in, which appears in the captured RTL_TCP STATUS lines - keep that file private.
"""
import argparse
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tab5_ft8 as t  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
TAB_Y = 676
TABS = ["map", "targets", "messages", "protocol", "signal", "events", "setup"]   # x = 104 + 178 * index


class Tab5Session:
    def __init__(self, port, key_path):
        self.port = port
        self.key = t.find_key(key_path)
        self.dev = t.Tab5(port)
        self.last = 0.0

    def close(self):
        try:
            self.dev.s.close()
        except Exception:
            pass

    def reopen(self):
        self.close()
        time.sleep(1.0)
        self.dev = t.Tab5(self.port)
        self.last = 0.0

    def cmd(self, line, prefix, wait=6.0):
        """Send one command and return the reply lines containing `prefix` (the session is re-authenticated when idle)."""
        if time.time() - self.last > 2.5:
            self.dev.authenticate(self.key)
        self.dev.s.reset_input_buffer()
        self.dev.send(line)
        self.last = time.time()
        out, end = [], time.time() + wait
        while time.time() < end:
            raw = self.dev.s.readline()
            if not raw:
                continue
            text = raw.decode("utf-8", "replace").rstrip()
            if prefix in text:
                out.append(text)
                end = min(end, time.time() + 0.6)   # multi-line replies arrive together
        return out

    def first(self, line, prefix, wait=6.0):
        reply = self.cmd(line, prefix, wait)
        return reply[0] if reply else ""

    def kv(self, text):
        return dict(re.findall(r'(\w+)=("[^"]*"|\S+)', text))

    def shot(self, path):
        self.close()
        subprocess.run([sys.executable, os.path.join(HERE, "tab5_ft8_shot.py"), "--port", self.port, path[:-4]],
                       capture_output=True, text=True, timeout=180)
        self.reopen()
        return os.path.exists(path)

    def wait(self, seconds):
        end = time.time() + seconds
        while time.time() < end:
            time.sleep(min(1.0, max(0.0, end - time.time())))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--port", required=True, help="the Tab5's serial port")
    p.add_argument("--key", help="pairing key file (default: the usual locations)")
    p.add_argument("--server", required=True, help="replay server as host:port")
    p.add_argument("--manifest", required=True, help="<iq>.json written by ais_make_iq.py")
    p.add_argument("--host", required=True, help="output of ais_decode_cu8 for the same IQ file")
    p.add_argument("--out", required=True, help="directory for the log and screenshots")
    p.add_argument("--passes", type=float, default=2.2, help="how many passes of the file to wait for")
    p.add_argument("--file-seconds", type=float, default=62.0, help="one pass including the server's gaps")
    p.add_argument("--no-shots", action="store_true")
    p.add_argument("--keep-server", action="store_true", help="do not restore the previous rtl_tcp server afterwards")
    p.add_argument("--allow-unrestorable-server", action="store_true",
                   help="run even though the Tab5 has no rtl_tcp server saved (the replay address stays saved afterwards)")
    a = p.parse_args()
    os.makedirs(a.out, exist_ok=True)
    host, _, port = a.server.rpartition(":")
    dev = Tab5Session(a.port, a.key)
    log = open(os.path.join(a.out, "device_log.txt"), "w", encoding="utf-8", newline="\n")

    def record(label, lines):
        for line in lines if isinstance(lines, list) else [lines]:
            if line:
                print(f"{label}: {line[:240]}")
                log.write(f"{label}: {line}\n")
        log.flush()

    before = dev.first("RTL_TCP STATUS", "RTL_TCP mode")
    record("before", before)
    previous = dev.kv(before)
    if (not previous.get("server") or previous["server"].startswith("(")) and not a.allow_unrestorable_server:
        # There is no command to clear the saved rtl_tcp server, so a unit with none configured cannot be put back.
        print("REFUSED: this Tab5 has no rtl_tcp server configured, and the replay server address could not be removed "
              "afterwards. Configure a server first, or pass --allow-unrestorable-server.")
        dev.close()
        return 2
    record("before", dev.first("RTL_HEALTH", "RTL_HEALTH_STATUS"))
    failures = []
    try:
        dev.cmd("RTL_UI OPEN HOME", "RTL_UI_OPEN")
        dev.cmd("RTL_TCP SOURCE remote", "RTL_TCP", 4)
        dev.cmd("RTL_STOP", "RTL_STOP", 12)
        dev.wait(3)
        # Arm the TEST REPLAY label BEFORE any replayed IQ can reach the receiver (it applies from the first block).
        marked = dev.first("RTL_AIS TEST ON", "RTL_AIS_TEST")
        record("test", marked)
        if "test=1" not in marked:   # the banner is the promise this script makes
            failures.append("RTL_AIS TEST ON was not acknowledged; replay data would not be labelled")
            raise RuntimeError("replay labelling is not active")
        pre = dev.first("RTL_AIS STATUS", "RTL_AIS_STATUS")   # the radio's own status line, before the replay source is selected
        record("pre", pre)
        if "test=1" not in pre:
            failures.append("RTL_AIS_STATUS does not show test=1 before the replay server is selected")
            raise RuntimeError("replay labelling is not active")
        record("host", dev.first(f"RTL_TCP HOST {host} {port}", "RTL_TCP HOST"))
        record("open", dev.first("RTL_UI OPEN AIS", "RTL_UI_OPEN"))
        dev.wait(5)
        armed = dev.first("RTL_AIS STATUS", "RTL_AIS_UI")
        record("armed", armed)
        if "test=1" not in armed:
            failures.append("the AIS screen did not carry the TEST REPLAY label after opening")
            raise RuntimeError("replay labelling is not active")
        dev.wait(a.passes * a.file_seconds)
        status = dev.cmd("RTL_AIS STATUS", "RTL_AIS", 6)
        record("status", status)
        record("tcp", dev.first("RTL_TCP STATUS", "RTL_TCP mode"))
        record("health", dev.first("RTL_HEALTH", "RTL_HEALTH_STATUS"))
        record("screen", dev.first("RTL_SCREEN_STATUS", "RTL_SCREEN_STATUS"))
        record("dsp", dev.cmd("RTL_DSP STATS", "RTL_DSP", 4))
        tcp_line = dev.first("RTL_TCP STATUS", "RTL_TCP mode")
        with open(os.path.join(a.out, "device_status.txt"), "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(status + [tcp_line]) + "\n")
        if not a.no_shots:
            for index, name in enumerate(TABS):
                dev.cmd(f"UI_TAP {104 + 178 * index} {TAB_Y}", "UI_TAP", 4)
                dev.wait(1.5)
                if name == "targets":
                    dev.cmd("UI_TAP 300 265", "UI_TAP", 4)   # select the first row so the detail panel is exercised
                    dev.wait(1.0)
                ok = dev.shot(os.path.join(a.out, f"ais_{index + 1}_{name}.png"))
                record("shot", f"{name} {'ok' if ok else 'FAILED'}")
                if not ok:
                    failures.append(f"screenshot of {name} failed")
        check = subprocess.run([sys.executable, os.path.join(HERE, "ais_check_replay.py"), a.manifest, "--host", a.host,
                                "--device", os.path.join(a.out, "device_status.txt"),
                                "--min-passes", f"{max(1.0, a.passes - 0.3):.2f}"], capture_output=True, text=True)
        record("check", check.stdout.strip().splitlines())
        if check.returncode != 0:
            failures.append("device / host comparison failed")
    except RuntimeError as exc:
        failures.append(str(exc))
    finally:
        dev.cmd("RTL_AIS TEST OFF", "RTL_AIS_TEST", 3)
        dev.cmd("RTL_UI OPEN HOME", "RTL_UI_OPEN")
        dev.wait(2)
        record("after_close", dev.cmd("RTL_AIS STATUS", "RTL_AIS", 6))
        record("after_close", dev.first("RTL_HEALTH", "RTL_HEALTH_STATUS"))
        if not a.keep_server and previous.get("server"):
            old_host, _, old_port = previous["server"].rpartition(":")
            dev.cmd("RTL_STOP", "RTL_STOP", 12)
            dev.wait(3)
            record("restore", dev.first(f"RTL_TCP HOST {old_host} {old_port}", "RTL_TCP HOST"))
            if previous.get("mode") and previous["mode"] != "remote":
                record("restore", dev.first(f"RTL_TCP SOURCE {previous['mode']}", "RTL_TCP"))
        dev.close()
        log.close()
    print("RESULT " + ("FAIL: " + "; ".join(failures) if failures else "PASS"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
