"""Physical acceptance measurement of a DV decoder build on the Tab5 (COM17). One run = one build ("label").

Waits for 240 s of uptime (the boot stabilisation period), measures idle, then four single-channel test points, each 8 s settle + 30 s window:
  analog  162.550 MHz  strong analog FM (NOAA weather) with only the DMR decoder: the DMR acquisition-screen workload
  noise   163.000 MHz  nothing there (noise), DMR decoder only
  dmr     152.1575 MHz DMR control channel with constant traffic, DMR decoder only
  nxdn48  463.250 MHz  known NXDN48 system, NXDN48 decoder only
  nxdn96  463.250 MHz  same channel through the NXDN96 decoder (CPU/memory comparison, not a decode test)
Per point it records: samples actually delivered to each decoder and the wall time spent in each (us per sample), channelizer us per raw pair,
worker busy, ring occupancy and drops, main-loop stalls (firmware RTL_MAIN_STALL), DV screen tick gaps (RTL_DV RENDER), task shares per core and
stack high-water marks (RTL_DV PROBE), internal/PSRAM free and largest blocks (RTL_MEM STATUS), and the voice synthesis timing counters.
The scanner and the task probe are ALWAYS stopped in a finally block, including on failure or Ctrl+C. Results go to <out>.json and <out>.txt.
Wall time is not CPU time: preemption inflates it. The probe's per-task shares are the CPU view.
"""
import json, re, sys, time
sys.path.insert(0, r"F:\Ai\OrcSDR\.claude\worktrees\dmr-digital-voice-review-dde01d\artifacts\digital-voice-integration\benchmarks")
from tab5_bench_lib import connect, run, find, kv

label = sys.argv[1] if len(sys.argv) > 1 else "run"
out = sys.argv[2] if len(sys.argv) > 2 else "accept_%s" % label
no_wait = "--no-wait" in sys.argv
POINTS = [("analog", "162.55:dmr"), ("noise", "163.0:dmr"), ("dmr", "152.1575:dmr"), ("nxdn48", "463.25:nxdn48"), ("nxdn96", "463.25:nxdn96")]
c = None
result = {"label": label, "points": {}}
log = open(out + ".txt", "w")
def say(s):
    print(s, flush=True); log.write(s + "\n"); log.flush()
def get():
    global c
    for _ in range(60):
        try:
            c = connect(); return
        except Exception: time.sleep(2)
    raise SystemExit("device did not answer")
def status(): return kv(find(run(c, "RTL_DV STATUS", 1.0), "RTL_DV_STATUS"))
def mem():
    m = find(run(c, "RTL_MEM STATUS", 1.0), "RTL_MEM"); return kv(m)
def uptime_s():
    t0 = time.time()
    while time.time() - t0 < 5:
        raw = c.serial.readline()
        if raw:
            m = re.search(rb'"uptime_ms":(\d+)', raw)
            if m: return int(m.group(1)) / 1000.0
    return None
def stop_all():
    try:
        run(c, "RTL_DV SCAN STOP", 1.5)
        run(c, "RTL_DV PROBE STOP", 1.0)
    except Exception as e:
        say("cleanup warning: %s" % str(e)[:80])
def count_stalls(secs):
    n = 0; t0 = time.time(); worst = 0
    while time.time() - t0 < secs:
        raw = c.serial.readline()
        if raw and b"RTL_MAIN_STALL stage=loop_gap" in raw:
            n += 1
            try: worst = max(worst, int(raw.split(b"elapsed_ms=")[1]))
            except Exception: pass
    return n, worst
def probe_dump():
    rows = {}
    for l in run(c, "RTL_DV PROBE DUMP", 3.0):
        if l.startswith("RTL_DV_PROBE") and "task=" in l:
            d = kv(l); rows["%s/core%s" % (d["task"], d["core"])] = {"share_pm": int(d["share_permille"]), "max_run_ms": int(d["max_run_ms"]), "stack_free_hwm": int(d.get("stack_free_hwm", 0))}
    return rows
try:
    get()
    run(c, "RTL_UI OPEN DIGITAL_VOICE", 3); time.sleep(2)
    if not no_wait:
        while True:
            u = uptime_s()
            if u is None or u >= 240: break
            say("waiting for boot stabilisation: uptime %.0f s of 240" % u); time.sleep(20)
    result["uptime_s"] = uptime_s()
    result["mem_start"] = mem()
    say("== %s: memory at start %s" % (label, result["mem_start"]))
    # idle (scan stopped)
    run(c, "RTL_DV SCAN STOP", 1.5); run(c, "RTL_DV RENDER RESET", 1.0)
    n, worst = count_stalls(30)
    tk = [l for l in run(c, "RTL_DV RENDER", 2.0) if l.startswith("RTL_DV_TICKS")]
    result["idle"] = {"stalls_30s": n, "worst_ms": worst, "ticks": tk[0] if tk else None}
    say("idle: stalls %d (worst %d ms) %s" % (n, worst, (tk[0][:110] if tk else "")))
    for name, plan in POINTS:
        r = [l for l in run(c, "RTL_DV PLAN " + plan, 1.5) if "RTL_DV_PLAN" in l]
        ok = False
        for _ in range(15):
            s = [l for l in run(c, "RTL_DV SCAN START", 2) if "RTL_DV_SCAN" in l]
            if s and "REFUSED" not in s[0]: ok = True; break
            time.sleep(3)
        if not ok: say("%s: scan did not start (%s)" % (name, r)); continue
        time.sleep(8)
        run(c, "RTL_DV PROBE STOP", 1.0); run(c, "RTL_DV PROBE START", 1.5); run(c, "RTL_DV RENDER RESET", 1.0)
        a = status(); t = time.time()
        n, worst = count_stalls(30)
        b = status(); dt = time.time() - t
        pr = probe_dump(); run(c, "RTL_DV PROBE STOP", 1.0)
        tk = [l for l in run(c, "RTL_DV RENDER", 2.0) if l.startswith("RTL_DV_TICKS")]
        au = find(run(c, "RTL_DV AUDIO", 1.2), "RTL_DV_AUDIO")
        d = lambda k: int(b.get(k, 0)) - int(a.get(k, 0))
        pt = {"plan": plan, "window_s": round(dt, 1), "stalls": n, "worst_stall_ms": worst,
              "pairs": d("pairs"), "smp_dmr": d("smp_dmr"), "smp_n48": d("smp_n48"), "smp_n96": d("smp_n96"),
              "us_ch": d("us_ch"), "us_dmr": d("us_dmr"), "us_n48": d("us_n48"), "us_n96": d("us_n96"),
              "ring_drops": d("ring_drops"), "ring_max": int(b.get("ring_max", 0)), "ring_pend": int(b.get("ring_pend", 0)),
              "worker_busy_pm": int(b.get("worker_busy_pm", 0)), "state_bytes": b.get("state_bytes"), "mask": b.get("mask"), "dec_internal": b.get("dec_internal"),
              "us_ch_per_pair": round(d("us_ch") / max(1, d("pairs")), 3),
              "us_dmr_per_sample": round(d("us_dmr") / max(1, d("smp_dmr")), 3) if d("smp_dmr") else None,
              "us_n48_per_sample": round(d("us_n48") / max(1, d("smp_n48")), 3) if d("smp_n48") else None,
              "us_n96_per_sample": round(d("us_n96") / max(1, d("smp_n96")), 3) if d("smp_n96") else None,
              "probe": pr, "ticks": tk[0] if tk else None,
              "synth": re.findall(r"synth_us=\S+", au), "mem": mem(), "events": d("events")}
        result["points"][name] = pt
        top = sorted(pr.items(), key=lambda kv_: -kv_[1]["share_pm"])[:4]
        say("%-7s dmr=%s n48=%s n96=%s us/smp, ch=%.3f us/pair busy=%s drops=%d stalls=%d probe=%s" % (name, pt["us_dmr_per_sample"], pt["us_n48_per_sample"], pt["us_n96_per_sample"],
            pt["us_ch_per_pair"], pt["worker_busy_pm"], pt["ring_drops"], n, [(k, v["share_pm"]) for k, v in top]))
        run(c, "RTL_DV SCAN STOP", 1.5); time.sleep(2)
    result["mem_end"] = mem()
finally:
    stop_all()
    try:
        json.dump(result, open(out + ".json", "w"), indent=1)
    except Exception as e:
        say("could not write json: %s" % str(e)[:80])
    say("scanner and probe stopped; results in %s.json/.txt" % out)
