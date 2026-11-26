"""Soak: MONITOR scan for N minutes. Every 30 s: DMR cost per sample, worker load, ring drops, main-loop stalls (firmware RTL_MAIN_STALL lines),
DV screen tick gaps (RTL_DV RENDER), decode counts; every 2 minutes a touch-to-screen latency check (tap a tab, time until the view changes)."""
import sys, time
sys.path.insert(0, r"F:\Ai\OrcSDR\.claude\worktrees\dmr-digital-voice-review-dde01d\artifacts\digital-voice-integration\benchmarks")
from tab5_bench_lib import connect, run, find, kv
mins = float(sys.argv[1]) if len(sys.argv) > 1 else 20
def get():
    for _ in range(90):
        try: return connect()
        except Exception: time.sleep(2)
    raise SystemExit('device did not answer for about 3 minutes')
c = get()
run(c, "RTL_UI OPEN DIGITAL_VOICE", 3); time.sleep(2)
r = [l for l in run(c, "RTL_DV SCAN START", 2) if "RTL_DV_SCAN" in l]   # the scan is already running on a long soak; a refusal is fine
print("scan:", r, flush=True)
def status():
    return kv(find(run(c, "RTL_DV STATUS", 1.0), "RTL_DV_STATUS"))
def latency(tab_x, target_view):
    t0 = time.time(); c.authenticate(create_key=False); c.send("RTL_UI TAP %d 675" % tab_x)
    for _ in range(20):
        c.send("RTL_DV STATUS"); end = time.time() + 0.25
        while time.time() < end:
            raw = c.serial.readline()
            if raw:
                l = raw.decode("utf-8", "replace")
                if l.startswith("RTL_DV_UI"):
                    try:
                        if int(kv(l)["view"]) == target_view: return (time.time() - t0) * 1000
                    except Exception: pass
    return None
a = status(); t_prev = time.time(); t_start = t_prev; n = 0
run(c, "RTL_DV RENDER RESET", 1.0)
worst_busy = 0; total_drops = 0; total_stalls = 0; lat = []
while time.time() - t_start < mins * 60:
    stalls = 0; end = time.time() + 30
    while time.time() < end:
        raw = c.serial.readline()
        if raw and b"RTL_MAIN_STALL stage=loop_gap" in raw: stalls += 1
    try:
        b = status(); dt = time.time() - t_prev; t_prev = time.time()
        per = (int(b["us_dmr"]) - int(a["us_dmr"])) / max(1, int(b["pairs"]) - int(a["pairs"]))
        drops = int(b["ring_drops"]) - int(a["ring_drops"])
        tk = [l for l in run(c, "RTL_DV RENDER", 2.0) if l.startswith("RTL_DV_TICKS")]
        gap = tk[0].split("gap_max_ms=")[1].split()[0] if tk else "?"
        n += 1; total_drops += drops; total_stalls += stalls; worst_busy = max(worst_busy, int(b.get("worker_busy_pm", 0)))
        line = "t=%4.0f dmr_us=%.2f busy_pm=%s drops=%d loop_stalls=%d tick_gap_max_ms=%s dmr_ev=%s dec_internal=%s" % (time.time() - t_start, per, b.get("worker_busy_pm"), drops, stalls, gap, b.get("dmr_ev"), b.get("dec_internal"))
        if n % 4 == 0:
            ms = latency(330, 1); ms2 = latency(118, 0); lat += [x for x in (ms, ms2) if x]
            line += "  touch_ms=%s/%s" % (None if ms is None else round(ms), None if ms2 is None else round(ms2))
        print(line, flush=True)
        a = b
        run(c, "RTL_DV RENDER RESET", 1.0)
    except Exception as e:
        print("error", str(e)[:60], flush=True); c = get()
print("SUMMARY minutes=%.0f samples=%d worst_busy_pm=%d total_ring_drops=%d total_loop_stalls=%d touch_ms_median=%s max=%s" % ((time.time() - t_start) / 60, n, worst_busy, total_drops, total_stalls,
      sorted(lat)[len(lat) // 2] if lat else None, round(max(lat)) if lat else None), flush=True)
