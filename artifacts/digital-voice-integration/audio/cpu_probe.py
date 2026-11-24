"""CPU probe: DMR-only decode load on the live channel (same method as benchmarks/tab5_device_bench.py live): core fractions from RTL_DV STATUS deltas."""
import sys, time
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "artifacts/digital-voice-integration/benchmarks"))
from tab5_bench_lib import connect, run, find, kv, dv
c = connect()
try:
    run(c, "RTL_UI OPEN DIGITAL_VOICE", 2.0); time.sleep(3)
    run(c, "RTL_DV SCAN STOP", 3.0); run(c, "RTL_DV MASK 1", 1.0); run(c, "RTL_DV PLAN %s" % (sys.argv[1] if len(sys.argv) > 1 else "152.1575"), 1.0)
    run(c, "RTL_DV SCAN START", 2.0); time.sleep(3)
    if len(sys.argv) < 3 or sys.argv[2] != "nohold": run(c, "RTL_DV SCAN HOLD", 1.5)
    time.sleep(8)
    for rep in range(3):
      u0, r0 = dv(c); w0 = time.time(); time.sleep(25); u1, r1 = dv(c); w1 = time.time()
      wall = (w1 - w0) * 1e6
      d = lambda k: int(r1.get(k, 0)) - int(r0.get(k, 0))
      print(dict(rep=rep, ch=r1.get("ch_hz"), window_s=round(w1 - w0, 1), channelizer=round(d("us_ch") / wall, 3), dmr=round(d("us_dmr") / wall, 3), ring_drops=d("ring_drops"), epoch=r1.get("epoch"), level=r1.get("level_milli"), busy_pm=r1.get("worker_busy_pm")), flush=True)
    u1, r1 = dv(c); w1 = time.time()
    wall = (w1 - w0) * 1e6
    d = lambda k: int(r1.get(k, 0)) - int(r0.get(k, 0))
    print(dict(window_s=round(w1 - w0, 1), channelizer=round(d("us_ch") / wall, 3), dmr=round(d("us_dmr") / wall, 3), blocks=d("blocks"), ring_drops=d("ring_drops"),
               level=r1.get("level_milli"), busy_pm=r1.get("worker_busy_pm"), dec_internal=r1.get("dec_internal"), state_bytes=r1.get("state_bytes"), loss=r1.get("loss")))
    run(c, "RTL_DV SCAN STOP", 3.0)
finally:
    c.close()
