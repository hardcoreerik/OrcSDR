"""Per-channel activity summary from hunt/results.jsonl, for tuning scan plans (dwell, order, which channels deserve time).
usage: channel_stats.py [results.jsonl]"""
import json, sys, collections
p = sys.argv[1] if len(sys.argv) > 1 else "hunt/results.jsonl"
r = [json.loads(l) for l in open(p) if l.strip()]
c = collections.defaultdict(lambda: dict(n=0, frames=0, ident=0, lv=[], first=None, last=None, name=""))
for x in r:
    d = c[x["hz"]]; d["n"] += 1; d["name"] = x["name"]; d["frames"] += (x["events"] > 0 or x["dmr_ev"] > 0); d["ident"] += x["dmr_id"] > 0
    if x["level"] is not None: d["lv"].append(x["level"])
    d["first"] = d["first"] or x["t"]; d["last"] = x["t"]
print(f"{len(r)} passes, {r[0]['t']} .. {r[-1]['t']}")
print(f"{'MHz':>10} {'passes':>6} {'frames%':>8} {'calls':>5} {'avg lvl':>8}  name")
for hz, d in sorted(c.items()):
    lv = sum(d["lv"]) / len(d["lv"]) if d["lv"] else 0
    print(f"{hz/1e6:10.4f} {d['n']:6d} {100*d['frames']/d['n']:7.0f}% {d['ident']:5d} {lv:8.0f}  {d['name']}")
