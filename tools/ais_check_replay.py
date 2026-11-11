#!/usr/bin/env python3
"""Compare an AIS decode with the synthetic IQ manifest written by tools/ais_make_iq.py.

  ais_decode_cu8 ais_fleet.cu8 > decode.txt
  python tools/ais_check_replay.py ais_fleet.cu8.json --host decode.txt
  python tools/ais_check_replay.py ais_fleet.cu8.json --host decode.txt --device status.txt   # + a Tab5 RTL_AIS STATUS capture
  python tools/ais_check_replay.py ais_fleet.cu8.json --pyais                                # third-party field check

--host    PKT / SUMMARY lines from tools/ais_decode_cu8.cpp. Checks: every transmitted payload was received exactly
          once per pass on the channel it was sent on, byte for byte; nothing else was received; and for the message
          types whose position fields OrcSDR decodes, latitude, longitude, speed, course and heading equal the values
          the generator encoded (independent encoder vs the OrcSDR parser).
--device  text containing an RTL_AIS_STATUS line from the Tab5 that replayed the same file. The device cannot be
          started on a file boundary, so the comparison is per pass: packet counts by type and by channel must be a
          whole multiple (within one pass) of the host run, with zero parse rejections.
--pyais   decode the manifest's NMEA sentences with the third-party pyais package (not part of this repository)
          and compare its fields with the generator's. This checks the generator, so it is not its own oracle.
Exit status 0 only if every requested check passes.
"""
import argparse
import json
import re
import sys


def load_host(path):
    packets, summary = [], None
    for line in open(path, encoding="utf-8", errors="replace"):
        line = line.strip()
        if line.startswith("PKT "):
            packets.append(dict(part.split("=", 1) for part in line.split()[1:]))
        elif line.startswith("SUMMARY "):
            summary = dict(part.split("=", 1) for part in line.split()[1:])
    return packets, summary


def check_host(manifest, path):
    packets, summary = load_host(path)
    failures = []
    if summary is None:
        return ["no SUMMARY line in the host decode"]
    expected = {}
    for tx in manifest["tx"]:
        expected.setdefault((tx["channel"], tx["payload_hex"]), []).append(tx)
    got = {}
    for packet in packets:
        got.setdefault((int(packet["ch"]), packet["hex"]), []).append(packet)
    missing = sum(max(0, len(v) - len(got.get(k, []))) for k, v in expected.items())
    extra = sum(max(0, len(v) - len(expected.get(k, []))) for k, v in got.items())
    if missing:
        failures.append(f"{missing} transmitted packets were not decoded")
        for key, txs in expected.items():
            if len(got.get(key, [])) < len(txs):
                failures.append("  missing: t=%.2f ch%d type %d mmsi %d offset %+.0f Hz amp %.2f" %
                                (txs[0]["t"], key[0], txs[0]["type"], txs[0]["mmsi"], txs[0]["offset_hz"], txs[0]["amplitude"]))
    if extra:
        failures.append(f"{extra} decoded packets were never transmitted (false or duplicated frames)")
    field_checks = 0
    static_checks = [0]
    for key, txs in expected.items():
        fields = txs[0]["fields"]
        for packet in got.get(key, []):
            if int(packet["type"]) != txs[0]["type"] or int(packet["id"]) != txs[0]["mmsi"]:
                failures.append(f"type/id mismatch for {key[1][:16]}...: {packet['type']}/{packet['id']}")
            if "lat" in fields and "lat_e7" not in packet:
                failures.append(f"type {txs[0]['type']} mmsi {fields['mmsi']}: the decoder exposed no position")
            if "lat" in fields and "lat_e7" in packet:
                field_checks += 1
                # The air interface carries 1/10000 minute (about 1.7e-6 degrees); allow one unit of that.
                if abs(int(packet["lat_e7"]) / 1e7 - fields["lat"]) > 2e-6 or abs(int(packet["lon_e7"]) / 1e7 - fields["lon"]) > 2e-6:
                    failures.append("position mismatch mmsi %d: decoded %s,%s expected %.6f,%.6f" %
                                    (fields["mmsi"], packet["lat_e7"], packet["lon_e7"], fields["lat"], fields["lon"]))
                if "sog_kn" in fields and fields["sog_kn"] > 0 and int(packet.get("sog_tenths", -1)) != round(fields["sog_kn"] * 10):
                    failures.append(f"speed mismatch mmsi {fields['mmsi']}: {packet.get('sog_tenths')}")
                if "cog_deg" in fields and "cog_tenths" in packet and int(packet["cog_tenths"]) != round(fields["cog_deg"] * 10):
                    failures.append(f"course mismatch mmsi {fields['mmsi']}: {packet.get('cog_tenths')}")
                if "heading" in fields and "heading" in packet and int(packet["heading"]) != fields["heading"]:
                    failures.append(f"heading mismatch mmsi {fields['mmsi']}: {packet.get('heading')}")
            # Static identity fields, where the parser decodes them (absent keys are simply not compared).
            for ours, theirs in (("name", "name"), ("callsign", "callsign"), ("destination", "dest"),
                                 ("ship_type", "ship_type"), ("imo", "imo"), ("nav_status", "nav"), ("dims", "dims")):
                if ours in fields and theirs not in packet:   # a field the generator encoded must reach the decoder output
                    failures.append(f"type {txs[0]['type']} mmsi {fields['mmsi']}: the decoder exposed no {ours}")
            for ours, theirs in (("name", "name"), ("callsign", "callsign"), ("destination", "dest")):
                if ours in fields and theirs in packet:
                    static_checks[0] += 1
                    if packet[theirs].replace("_", " ") != fields[ours]:
                        failures.append(f"{ours} mismatch mmsi {fields['mmsi']}: {packet[theirs]!r} != {fields[ours]!r}")
            for ours, theirs in (("ship_type", "ship_type"), ("imo", "imo"), ("nav_status", "nav")):
                if ours in fields and theirs in packet:
                    static_checks[0] += 1
                    if int(packet[theirs]) != fields[ours]:
                        failures.append(f"{ours} mismatch mmsi {fields['mmsi']}: {packet[theirs]} != {fields[ours]}")
            if "dims" in fields and "dims" in packet:
                static_checks[0] += 1
                if [int(v) for v in packet["dims"].split("/")] != fields["dims"]:
                    failures.append(f"dimensions mismatch mmsi {fields['mmsi']}: {packet['dims']}")
    print("host: %d transmitted, %d decoded, %d missing, %d extra, %d position field sets and %d static fields compared, "
          "types %s, digest %s" % (len(manifest["tx"]), len(packets), missing, extra, field_checks, static_checks[0],
                                   summary.get("types"), summary.get("digest")))
    return failures


def parse_types(text):
    out = {}
    for part in text.split(","):
        if ":" in part:
            k, v = part.split(":")
            out[int(k)] = int(v)
    return out


def check_device(manifest, host_path, device_path, min_passes=1.0, allow_loss=False):
    _, summary = load_host(host_path)
    text = open(device_path, encoding="utf-8", errors="replace").read()
    lines = [line for line in text.splitlines() if "RTL_AIS_STATUS" in line]
    if not lines or summary is None:
        return ["no RTL_AIS_STATUS line (or no host SUMMARY) to compare"]
    status = dict(part.split("=", 1) for part in lines[-1].split() if "=" in part)
    failures = []
    host_types, device_types = parse_types(summary["types"]), parse_types(status.get("types", ""))
    host_total, device_total = int(summary["packets"]), int(status["packets"])
    passes = device_total / host_total if host_total else 0
    lo, hi = int(passes), int(passes) + 1
    print("device: packets=%d (%.2f passes of %d) ch1_ok=%s ch2_ok=%s parse_rejected=%s lost_samples=%s loss_events=%s link=%s source=%s" %
          (device_total, passes, host_total, status.get("ch1_ok"), status.get("ch2_ok"), status.get("parse_rejected"),
           status.get("lost_samples"), status.get("loss_events"), status.get("link"), status.get("source")))
    if device_total < host_total:
        failures.append("the device has not completed one pass of the file")
    if passes < min_passes:
        failures.append(f"only {passes:.2f} passes of the file were seen, at least {min_passes:.2f} were expected")
    if not allow_loss and (int(status.get("lost_samples", 0)) != 0 or int(status.get("loss_events", 0)) != 0):
        failures.append("the device reports lost IQ (lost_samples=%s loss_events=%s)" % (status.get("lost_samples"), status.get("loss_events")))
    if int(status.get("test", 0)) != 1:
        failures.append("the device was not marked as a test replay (test=%s)" % status.get("test"))
    for msg_type, count in host_types.items():
        seen = device_types.get(msg_type, 0)
        if not (lo * count <= seen <= hi * count):
            failures.append(f"type {msg_type}: device {seen}, expected {lo * count}..{hi * count} ({count} per pass)")
    for msg_type in device_types:
        if msg_type not in host_types:
            failures.append(f"type {msg_type}: {device_types[msg_type]} on the device, never transmitted")
    for channel in (1, 2):
        per_pass, seen = int(summary[f"ch{channel}"]), int(status[f"ch{channel}_ok"])
        dup = 0   # duplicates are counted separately and never reach the packet count
        if not (lo * per_pass <= seen - dup <= hi * per_pass):
            failures.append(f"channel {channel}: device {seen}, expected {lo * per_pass}..{hi * per_pass}")
    if int(status.get("parse_rejected", 0)) != 0:
        failures.append("the device parser rejected CRC-valid packets")
    if passes == int(passes) and int(passes) >= 1:
        want = (int(summary["digest"], 16) * int(passes)) % (1 << 64)
        if int(status["digest"], 16) != want:
            failures.append("whole-pass packet digest differs from the host decode")
        else:
            print("device: packet digest equals %d x the host digest (identical packet set)" % int(passes))
    return failures


def check_pyais(manifest):
    try:
        from pyais import decode
    except ImportError:
        return ["pyais is not installed (pip install pyais); third-party check not run"]
    failures, compared = [], 0
    for tx in manifest["tx"]:
        msg = decode(*[s.encode() for s in tx["nmea"]]).asdict()
        fields = tx["fields"]
        if msg["msg_type"] != tx["type"] or msg["mmsi"] != tx["mmsi"]:
            failures.append(f"t={tx['t']}: pyais type/mmsi {msg['msg_type']}/{msg['mmsi']} != {tx['type']}/{tx['mmsi']}")
        pairs = [("lat", "lat", 2e-6), ("lon", "lon", 2e-6), ("sog_kn", "speed", 0.051), ("cog_deg", "course", 0.051),
                 ("heading", "heading", 0), ("nav_status", "status", 0), ("ship_type", "ship_type", 0), ("imo", "imo", 0),
                 ("draught_m", "draught", 0.051), ("aid_type", "aid_type", 0)]
        for ours, theirs, tolerance in pairs:
            if ours in fields and theirs in msg:
                compared += 1
                if abs(float(msg[theirs]) - float(fields[ours])) > tolerance:
                    failures.append(f"t={tx['t']} type {tx['type']}: {ours} {fields[ours]} but pyais {theirs}={msg[theirs]}")
        for ours, theirs in (("name", "shipname"), ("name", "name"), ("callsign", "callsign"), ("destination", "destination"),
                             ("vendor", "vendor_id")):
            if ours in fields and theirs in msg and isinstance(msg[theirs], str):
                compared += 1
                if msg[theirs].strip().rstrip("@") != fields[ours]:
                    failures.append(f"t={tx['t']} type {tx['type']}: {ours} {fields[ours]!r} but pyais {theirs}={msg[theirs]!r}")
        if "virtual" in fields and "virtual_aid" in msg:
            compared += 1
            if bool(msg["virtual_aid"]) != fields["virtual"]:
                failures.append(f"t={tx['t']}: virtual flag differs")
    import pyais
    print("pyais %s: %d sentences groups decoded, %d fields compared" % (pyais.__version__, len(manifest["tx"]), compared))
    return failures


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("manifest")
    p.add_argument("--host")
    p.add_argument("--device")
    p.add_argument("--pyais", action="store_true")
    p.add_argument("--min-passes", type=float, default=1.0, help="fewest whole passes of the file the device must have seen")
    p.add_argument("--allow-loss", action="store_true", help="do not fail on lost_samples / loss_events")
    a = p.parse_args()
    manifest = json.load(open(a.manifest))
    failures = []
    if a.host:
        failures += check_host(manifest, a.host)
    if a.device:
        if not a.host:
            sys.exit("--device needs --host")
        failures += check_device(manifest, a.host, a.device, a.min_passes, a.allow_loss)
    if a.pyais:
        failures += check_pyais(manifest)
    for failure in failures:
        print("FAIL " + failure)
    print("RESULT " + ("FAIL" if failures else "PASS"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
