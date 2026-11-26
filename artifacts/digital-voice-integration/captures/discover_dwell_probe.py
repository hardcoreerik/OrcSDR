"""Start DISCOVER by touch and log how long the receiver stays on each channel (RTL_DV STATUS ch_hz sampled about once a second)."""
import sys, time
sys.path.insert(0, r"F:\Ai\OrcSDR\.claude\worktrees\dmr-digital-voice-review-dde01d\artifacts\digital-voice-integration\benchmarks")
from tab5_bench_lib import connect, run, find, kv
mins = float(sys.argv[1]) if len(sys.argv) > 1 else 6
c = connect(); time.sleep(2)
run(c, "RTL_UI OPEN DIGITAL_VOICE", 3); time.sleep(2)
run(c, "RTL_DV SCAN STOP", 1.0); run(c, "RTL_UI TAP 740 598", 1.0); time.sleep(1); run(c, "RTL_UI TAP 159 599", 1.0)
t0 = time.time(); cur = None; since = t0; runs = []
while time.time() - t0 < mins * 60:
    st = kv(find(run(c, "RTL_DV STATUS", 0.8), "RTL_DV_STATUS"))
    ch = st.get("ch_hz")
    now = time.time()
    if ch != cur:
        if cur is not None: runs.append((cur, round(now - since, 1)))
        cur, since = ch, now
print("visits (ch_hz, seconds on channel):")
for r in runs: print(r)
d = [s for _, s in runs]
print("n", len(d), "median", sorted(d)[len(d) // 2] if d else None)
