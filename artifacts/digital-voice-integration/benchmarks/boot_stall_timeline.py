"""Reset the Tab5 (RTL_RESET), start a DV MONITOR scan about 20 s after boot, then log for N seconds: how many main-loop stalls (RTL_MAIN_STALL
loop_gap) fall in each 15 s bucket, and every other firmware line (first occurrence of each kind, with its time) so the boot-time activity that
ends when the stalls end can be identified."""
import sys, time, collections
sys.path.insert(0, r"F:\Ai\OrcSDR\.claude\worktrees\dmr-digital-voice-review-dde01d\artifacts\digital-voice-integration\benchmarks")
from tab5_bench_lib import connect, run
total = float(sys.argv[1]) if len(sys.argv) > 1 else 330
c = connect()
run(c, "RTL_RESET", 1.0)
t_reset = time.time()
time.sleep(4)
c.serial.close()
time.sleep(10)
c = None
for _ in range(20):
    try: c = connect(); break
    except Exception: time.sleep(2)
run(c, "RTL_UI OPEN DIGITAL_VOICE", 2); time.sleep(2)
run(c, "RTL_DV MONITOR ALL", 1.5)
for _ in range(15):
    r = [l for l in run(c, "RTL_DV SCAN START", 2) if "RTL_DV_SCAN" in l]
    if r and "REFUSED" not in r[0]: break
    time.sleep(3)
print("scan started %.0f s after reset: %s" % (time.time() - t_reset, r), flush=True)
buckets = collections.Counter(); kinds = {}
last_key = None
while time.time() - t_reset < total:
    raw = c.serial.readline()
    if not raw: continue
    t = time.time() - t_reset
    l = raw.decode("utf-8", "replace").strip()
    if l.startswith('{"type"'): continue
    if "RTL_MAIN_STALL stage=loop_gap" in l:
        buckets[int(t // 15)] += 1; continue
    key = " ".join(l.split()[:3])[:48]
    if key not in kinds:
        kinds[key] = [t, 0]
        print("t=%5.0f first: %s" % (t, l[:150]), flush=True)
    kinds[key][1] += 1
print("stalls per 15 s bucket:", {k * 15: v for k, v in sorted(buckets.items())})
print("line kinds (first time, count):")
for k, (t, n) in sorted(kinds.items(), key=lambda kv: kv[1][0]): print("  %5.0f s  x%-4d %s" % (t, n, k))
