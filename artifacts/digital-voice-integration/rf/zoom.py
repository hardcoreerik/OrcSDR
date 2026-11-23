import subprocess, sys
c=float(sys.argv[1]); span=float(sys.argv[2]); g=sys.argv[3]; secs=sys.argv[4]
lo,hi=(c-span/2)*1e6,(c+span/2)*1e6
p=subprocess.run(["rtl_power","-T","-f",f"{lo:.0f}:{hi:.0f}:1000","-i","2","-e",secs,"-g",g,"-"],capture_output=True,text=True)
acc={}
for line in p.stdout.splitlines():
    parts=line.split(",")
    if len(parts)<8: continue
    f0=float(parts[2]); step=float(parts[4])
    for i,x in enumerate(parts[6:]):
        try: v=float(x)
        except ValueError: continue
        if v==v: acc.setdefault(round((f0+(i+0.5)*step)/1000),[]).append(v)
rows=sorted((k,max(v),sum(v)/len(v)) for k,v in acc.items())
top=max(r[2] for r in rows)
print("kHz      avg   max   (avg bar)")
for k,mx,av in rows:
    if av>top-25 : print(f"{k:8d} {av:6.1f} {mx:6.1f} "+"#"*int(max(0,av-(top-25))))
