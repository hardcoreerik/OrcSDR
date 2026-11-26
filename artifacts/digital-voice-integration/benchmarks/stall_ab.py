import sys,time
sys.path.insert(0,r"F:\Ai\OrcSDR\.claude\worktrees\dmr-digital-voice-review-dde01d\artifacts\digital-voice-integration\benchmarks")
from tab5_bench_lib import connect, run
c=None
for _ in range(10):
    try: c=connect(); break
    except Exception: time.sleep(3)
def count(label,secs=20):
    t0=time.time(); g=[]
    while time.time()-t0<secs:
        raw=c.serial.readline()
        if raw:
            l=raw.decode("utf-8","replace")
            if "RTL_MAIN_STALL stage=loop_gap" in l:
                try: g.append(int(l.split("elapsed_ms=")[1]))
                except Exception: pass
    print("%-34s stalls %2d in %ds  total %5d ms"%(label,len(g),secs,sum(g)),flush=True)
count("baseline (scanning, audio on)")
run(c,"RTL_DV AUDIO DISABLE",1.0); count("voice audio DISABLED")
run(c,"RTL_DV AUDIO ENABLE",1.0)
run(c,"RTL_DV AUDIO SAVE OFF",1.0); count("speech recording OFF")
run(c,"RTL_DV SCAN STOP",1.0); count("scan stopped (idle)")
