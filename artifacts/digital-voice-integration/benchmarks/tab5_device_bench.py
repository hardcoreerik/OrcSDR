"""Tab5 device benchmark (COM17): CPU, memory and acquisition measured by the firmware's own counters.
live  : the Tab5's own antenna on a channel, each decoder mask (CPU / memory under real DSP load, no PC, no replay)
replay: SD replays of the real call and of two degraded copies, polled every 0.5 s (acquisition time, host/device parity)
usage: tab5_device_bench.py live|replay"""
import sys, time, json, re
from pathlib import Path
sys.path.insert(0, "F:/Ai/OrcSDR/.claude/worktrees/dmr-digital-voice-review-dde01d/tools")
from help_media import Tab5
mode = sys.argv[1]
OUT = Path(__file__).parent / f"device_{mode}.jsonl"
c = Tab5("COM17", Path("F:/Ai/OrcSDR/.orclink/ui-doc.key")); c.authenticate(create_key=False)
def kv(line): return {k: v for k, v in re.findall(r'(\w+)=("[^"]*"|\S+)', line)}
def ask(cmd, pre, t=10):
    c.send(cmd)
    try: return c.wait((pre,), t)
    except TimeoutError:
        if cmd.startswith(("RTL_DV SCAN", "RTL_DV MASK", "RTL_DV PLAN")):   # the receiver can take a while to stop; keep going
            time.sleep(3); return ""
        raise
def dv():
    c.send("RTL_DV STATUS"); u = c.wait(("RTL_DV_UI",), 8); raw = c.wait(("RTL_DV_STATUS",), 8)
    if DEBUG: print("RAW", raw[:200], flush=True)
    return u, kv(raw)
DEBUG = "--debug" in sys.argv
def mem():
    c.send("RTL_MEM STATUS"); a = kv(c.wait(("RTL_MEM ",), 8))
    try: b = kv(c.wait(("RTL_MEM_MIN",), 4))
    except TimeoutError: b = {}
    return a, b
def log(rec):
    rec["t"] = time.strftime("%Y-%m-%d %H:%M:%S"); print(json.dumps(rec), flush=True); open(OUT, "a").write(json.dumps(rec) + "\n")
def open_dash():
    for _ in range(4):
        c.send("RTL_UI OPEN DIGITAL_VOICE"); c.wait(("RTL_UI",), 8); time.sleep(4)
        u, _ = dv()
        if "open=1" in u: return
    raise SystemExit("cannot open dashboard")
try:
    ask("RTL_DRIVER GAINMODE AUTO", "RTL_DRIVER_RESULT")
    open_dash()
    m, mn = mem(); log(dict(step="dashboard_open_idle", **{k: m.get(k) for k in ("psram_free", "internal_free", "internal_largest")}, **{"min_" + k: mn.get(k) for k in ("psram_min", "internal_min")}))
    if mode == "live":
        for mask, tag in ((1, "dmr"), (2, "nxdn48"), (4, "nxdn96"), (7, "all three forced")):
            ask("RTL_DV SCAN STOP", "RTL_DV_SCAN"); ask(f"RTL_DV MASK {mask}", "RTL_DV_MASK"); ask("RTL_DV PLAN 152.1575", "RTL_DV_PLAN")
            ask("RTL_DV SCAN START", "RTL_DV_SCAN"); time.sleep(3); ask("RTL_DV SCAN HOLD", "RTL_DV_SCAN")
            time.sleep(8)
            u0, r0 = dv(); w0 = time.time()
            time.sleep(30)
            u1, r1 = dv(); w1 = time.time()
            wall = (w1 - w0) * 1e6
            def d(k): return int(r1.get(k, 0)) - int(r0.get(k, 0))
            m, mn = mem()
            log(dict(step=f"live_hold_{tag}", mask=mask, window_s=round(w1 - w0, 1), pairs=d("pairs"), blocks=d("blocks"),
                     dsp_channelizer_core_fraction=round(d("us_ch") / wall, 3), dmr_core_fraction=round(d("us_dmr") / wall, 3),
                     nxdn48_core_fraction=round(d("us_n48") / wall, 3), nxdn96_core_fraction=round(d("us_n96") / wall, 3),
                     dsp_block_us_avg=int(r1.get("blk_us_avg", 0)), dsp_block_us_max_since_start=int(r1.get("blk_us_max", 0)), ring_drops=r1.get("ring_drops"), loss=r1.get("loss"),
                     level_milli=r1.get("level_milli"), decoder_state_bytes=r1.get("state_bytes"), psram_free=m.get("psram_free"), internal_free=m.get("internal_free"),
                     internal_largest=m.get("internal_largest"), min_psram=mn.get("psram_min"), min_internal=mn.get("internal_min")))
        ask("RTL_DV SCAN STOP", "RTL_DV_SCAN")
    else:
        for sd, tag in (("/orcsdr/dv_live_call_152p1575.cu8", "call_clean"), ("/orcsdr/dv_call_noise6.cu8", "call_plus_noise_6db"), ("/orcsdr/dv_call_noise3.cu8", "call_plus_noise_3db")):
            ask("RTL_DV SCAN STOP", "RTL_DV_SCAN"); ask("RTL_DV MASK 1", "RTL_DV_MASK")
            t0 = time.time(); first_ev = first_id = None
            ask(f"RTL_DV REPLAY {sd} 152157500", "RTL_DV_REPLAY")
            last = {}
            while time.time() - t0 < 150:
                time.sleep(0.5); _, r = dv(); last = r
                if first_ev is None and int(r.get("dmr_ev", 0)) > 0: first_ev = round(time.time() - t0, 1)
                if first_id is None and int(r.get("dmr_id", 0)) > 0: first_id = round(time.time() - t0, 1)
                if int(r.get("pairs", 0)) >= 3360000 and int(r.get("ring_pend", 1)) == 0: break
            time.sleep(2); u, r = dv(); m, mn = mem()
            log(dict(step=f"replay_{tag}", wall_s=round(time.time() - t0, 1), first_dmr_event_wall_s=first_ev, first_identity_wall_s=first_id, events=r.get("events"), dmr_ev=r.get("dmr_ev"),
                     dmr_id=r.get("dmr_id"), worker_busy_pm=r.get("worker_busy_pm"), ring_max=r.get("ring_max"), ring_drops=r.get("ring_drops"), psram_free=m.get("psram_free"), internal_free=m.get("internal_free")))
finally:
    try: ask("RTL_DV SCAN STOP", "RTL_DV_SCAN", 4)
    except Exception: pass
    c.close()
