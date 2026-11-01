"""Decode RTL2832U EP0 control transfers from a USBPcap pcapng, split by the stimulus
events.json timestamps, and annotate them (tuner I2C writes via the RTL2832U repeater,
demod page writes/reads). Observed fields only; annotations are labels, not claims.

Usage: decode_ep0.py PCAP EVENTS_JSON BUS DEVADDR OUT_TXT
"""
import json, subprocess, sys
from collections import OrderedDict

TSHARK = r"C:\Program Files\Wireshark\tshark.exe"
pcap, events_path, bus, addr, out = sys.argv[1:6]
fields = ["frame.number", "frame.time_epoch", "usb.irp_id", "usb.irp_info.direction",
          "usb.bmRequestType", "usb.setup.wValue", "usb.setup.wIndex", "usb.setup.wLength",
          "usb.usbd_status", "usb.data_fragment", "usb.control.Response", "usb.control_stage"]
cmd = [TSHARK, "-r", pcap, "-Y",
       f"usb.bus_id=={bus} && usb.device_address=={addr} && usb.transfer_type==0x02",
       "-T", "fields", "-E", "separator=\t", "-E", "occurrence=f"]
for f in fields:
    cmd += ["-e", f]
rows = [dict(zip(fields, l.split("\t"))) for l in
        subprocess.run(cmd, capture_output=True, text=True, check=True).stdout.splitlines()]

# tshark prints wIndex in decimal and wValue in hex.
pending, xfers = OrderedDict(), []
for r in rows:
    irp = r["usb.irp_id"]
    if r["usb.bmRequestType"]:                       # submit carrying the setup packet
        pending[irp] = dict(frame=int(r["frame.number"]), t=float(r["frame.time_epoch"]),
                            req=int(r["usb.bmRequestType"], 16), wValue=int(r["usb.setup.wValue"] or "0", 16),
                            wIndex=int(r["usb.setup.wIndex"] or "0", 0), wLength=int(r["usb.setup.wLength"] or "0"),
                            out=r["usb.data_fragment"].replace(":", ""), status=None, inp="")
        xfers.append(pending[irp])
    elif irp in pending:                             # completion
        x = pending.pop(irp)
        x["status"] = r["usb.usbd_status"]
        x["inp"] = (r["usb.control.Response"] or r["usb.data_fragment"]).replace(":", "")
unpaired = len(pending)

events = json.load(open(events_path))
bounds = [(e["stage"], e.get("t_start", e["t"])) for e in events]


def stage_of(t):
    name = "pre-open"
    for s, t0 in bounds:
        if t >= t0:
            name = s
    return name


def label(x):
    v, i, req = x["wValue"], x["wIndex"], x["req"]
    d = x["out"] if req == 0x40 else x["inp"]
    b = bytes.fromhex(d) if d else b""
    if i in (0x0610, 0x0600) and (v & 0xff) in (0x34, 0x74, 0xc8):
        if req == 0x40 and len(b) >= 2:
            return f"TUNER W reg{b[0]:02x}=" + " ".join(f"{c:02x}" for c in b[1:])
        if req == 0x40 and len(b) == 1:
            return f"TUNER ptr {b[0]:02x}"
        return f"TUNER R {len(b)}B " + " ".join(f"{c:02x}" for c in b)
    if req == 0x40 and (v & 0xff) == 0x20 and i & 0xfff0 == 0x0010:
        page = i & 0x0f
        return ("REPEATER " if page == 1 and v >> 8 == 1 else "") + f"DEMOD W p{page} r{v >> 8:02x}=" + " ".join(f"{c:02x}" for c in b)
    if (v & 0xff) == 0x20 and req == 0xc0:
        return f"DEMOD R p{i:02x} r{v >> 8:02x} -> " + " ".join(f"{c:02x}" for c in b)
    if req == 0x40:
        return f"SYS/USB W v={v:04x} i={i:04x} " + " ".join(f"{c:02x}" for c in b)
    return f"SYS/USB R v={v:04x} i={i:04x} -> " + " ".join(f"{c:02x}" for c in b)


with open(out, "w") as fh:
    fh.write(f"# {pcap}\n# bus={bus} dev={addr} control_transfers={len(xfers)} unpaired_submits={unpaired}\n")
    fh.write("# frame\tt_rel\treq\twValue\twIndex\twLen\tstatus\tlabel\n")
    t0 = xfers[0]["t"] if xfers else 0
    cur = None
    for x in xfers:
        s = stage_of(x["t"])
        if s != cur:
            fh.write(f"\n## stage {s}\n")
            cur = s
        fh.write(f"{x['frame']}\t{x['t'] - t0:9.4f}\t{x['req']:02x}\t{x['wValue']:04x}\t{x['wIndex']:04x}\t"
                 f"{x['wLength']}\t{x['status']}\t{label(x)}\n")
print(out, "transfers", len(xfers), "unpaired", unpaired)
