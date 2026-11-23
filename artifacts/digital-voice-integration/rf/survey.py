import subprocess, statistics, sys, csv, io
# (label, centre MHz)
T=[("Terra DMR T3 ctrl Eugene",152.1575),("Terra DMR Oakridge",152.345),
   ("HAM DMR 442.425 CC1 Blanton Hts",442.425),("HAM DMR 444.425 CC1 SantaClara",444.425),("HAM DMR 444.2375 CC9",444.2375),
   ("NXDN Silke ctrl Eugene",454.225),("NXDN Silke ctrl Quarry Hill",454.350),("NXDN Silke ctrl Hansen Butte",454.2125),
   ("NXDN Silke ctrl Prairie Mtn",454.4375),("NXDN Silke ctrl Buck Mtn",463.375),("NXDN UO ctrl",453.950),("NXDN OSH ctrl",462.175)]
for label,f in T:
    lo,hi=(f-0.025)*1e6,(f+0.025)*1e6
    p=subprocess.run(["rtl_power","-T","-f",f"{lo:.0f}:{hi:.0f}:1000","-i","2","-e","6","-g","40","-"],capture_output=True,text=True)
    rows=[]
    for line in p.stdout.splitlines():
        parts=line.split(",")
        if len(parts)<8: continue
        vals=[]
        for x in parts[6:]:
            try:
                v=float(x)
                if v==v: vals.append(v)
            except ValueError: pass
        if vals: rows.append(vals)
    if not rows: print(f"{label:34s} {f:9.4f}  NO DATA {p.stderr[-120:]!r}"); continue
    n=min(len(r) for r in rows); rows=[r[:n] for r in rows]; bw=(hi-lo)/n
    avg=[sum(r[i] for r in rows)/len(rows) for i in range(n)]
    mx=[max(r[i] for r in rows) for i in range(n)]
    med=statistics.median(avg)
    idx=[i for i in range(n) if abs(lo+(i+0.5)*bw-f*1e6)<=max(6250,bw*1.5)] or [min(range(n),key=lambda i:abs(lo+(i+0.5)*bw-f*1e6))]
    pk=max(avg[i] for i in idx); pkmax=max(mx[i] for i in idx)
    print(f"{label:34s} {f:9.4f} MHz  chan avg-peak {pk:6.1f} dB  hold {pkmax:6.1f}  floor(med) {med:6.1f}  over floor {pk-med:5.1f} dB",flush=True)
