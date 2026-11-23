import subprocess, statistics, sys
lo,hi,gain,secs=sys.argv[1],sys.argv[2],sys.argv[3],sys.argv[4]
p=subprocess.run(["rtl_power","-T","-f",f"{float(lo)*1e6:.0f}:{float(hi)*1e6:.0f}:3125","-i","5","-e",secs,"-g",gain,"-c","20%","-"],capture_output=True,text=True)
acc={}
for line in p.stdout.splitlines():
    parts=line.split(",")
    if len(parts)<8: continue
    f0=float(parts[2]); f1=float(parts[3]); step=float(parts[4]); vals=[]
    for x in parts[6:]:
        try:
            v=float(x); vals.append(v if v==v else None)
        except ValueError: vals.append(None)
    for i,v in enumerate(vals):
        if v is None: continue
        f=f0+(i+0.5)*step
        a=acc.setdefault(round(f/1000.0),[]); a.append(v)
fr=sorted(acc)
avg={k:sum(acc[k])/len(acc[k]) for k in fr}
med=statistics.median(avg.values())
cand=[(avg[k]-med,k) for k in fr if avg[k]-med>=6]
cand.sort(reverse=True)
print(f"gain {gain} dB, floor(median) {med:.1f} dB, bins {len(fr)}")
seen=[]
for d,k in cand:
    if any(abs(k-s)<12 for s in seen): continue
    seen.append(k); print(f"  {k/1000:9.3f} MHz  +{d:5.1f} dB")
    if len(seen)>=25: break
