"""Count the firmware's own RTL_MAIN_STALL loop_gap messages (loop gap >= 500 ms) over N seconds. Starts MONITOR scanning first so the receiver is busy."""
import sys,time
sys.path.insert(0,r"F:\Ai\OrcSDR\.claude\worktrees\dmr-digital-voice-review-dde01d\artifacts\digital-voice-integration\benchmarks")
from tab5_bench_lib import connect, run
secs=float(sys.argv[1]) if len(sys.argv)>1 else 30
c=None
for _ in range(10):
    try: c=connect(); break
    except Exception: time.sleep(3)
run(c,"RTL_UI OPEN DIGITAL_VOICE",3); time.sleep(3)
run(c,"RTL_DV MONITOR ALL",1.5)
for _ in range(10):
    r=[l for l in run(c,"RTL_DV SCAN START",2) if "RTL_DV_SCAN" in l]
    if r and "REFUSED" not in r[0]: break
    time.sleep(3)
time.sleep(5)
t0=time.time(); gaps=[]
try:
  while time.time()-t0<secs:
    raw=c.serial.readline()
    if raw:
        l=raw.decode("utf-8","replace")
        if "RTL_MAIN_STALL" in l:
            try: gaps.append(int(l.split("elapsed_ms=")[1]))
            except Exception: pass
finally:
  run(c,"RTL_DV SCAN STOP",1.5)      # always leave the scanner stopped, also on failure
print("stalls >=500 ms in %.0f s: %d  max %d ms  total %d ms"%(secs,len(gaps),max(gaps) if gaps else 0,sum(gaps)))
