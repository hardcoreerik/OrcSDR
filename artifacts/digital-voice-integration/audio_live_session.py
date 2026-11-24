"""Stage E/F: live RF on the Tab5 (COM17, V4L + 140/440 MHz antenna, bias tee OFF) through the scanner with native voice audio.
Receive only. Logs RTL_DV AUDIO STATUS / RTL_DV STATUS / RTL_WIFI_COEX_STATUS every `period` s to audio/live_session.jsonl and takes one PNG
when speech is first decoded. usage: audio_live_session.py <minutes> [plan]"""
import json, subprocess, sys, time
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "artifacts/digital-voice-integration/benchmarks"))
from tab5_bench_lib import connect, run, find, kv
OUT = Path(__file__).parent / "audio"; OUT.mkdir(exist_ok=True)
minutes = float(sys.argv[1]) if len(sys.argv) > 1 else 10
plan = sys.argv[2] if len(sys.argv) > 2 else "152.1575:dmr,152.685:dmr,152.345:dmr,153.068:dmr"
def snap(c, tag):
    a = kv(find(run(c, "RTL_DV AUDIO STATUS", 1.0), "RTL_DV_AUDIO "))
    d = kv(find(run(c, "RTL_DV STATUS", 1.0), "RTL_DV_STATUS"))
    u = find(run(c, "RTL_DV STATUS", 1.0), "RTL_DV_UI")
    s = kv(find(run(c, "RTL_WIFI_COEX_STATUS", 1.0), "RTL_WIFI_COEX_STATUS"))
    sysl = kv(find(run(c, "RTL_SUPPORT REPORT", 1.2), "ORC_SUPPORT_SYSTEM"))
    rec = dict(tag=tag, t=time.strftime("%H:%M:%S"), ch=d.get("ch_hz"), link=d.get("link"), acq=d.get("dmr_acq"), ev=d.get("dmr_ev"), idv=d.get("dmr_id"), level=d.get("level_milli"),
               busy=d.get("worker_busy_pm"), loss=d.get("loss"), ring_drops=d.get("ring_drops"), scan=(u.split("scan=")[1].split(" plan=")[0] if "scan=" in u else None),
               **{k: a.get(k) for k in ("phase", "listen", "slot", "groups", "speech", "silence", "errors", "underruns", "flushes", "qual", "prot", "unq", "calls", "gap_ms", "latency_ms", "nfm_blocked", "synth_us", "buffer_ms", "pump_max_us")},
               audio_chunks=s.get("audio_chunks"), audio_drops=s.get("audio_drops"), uptime_ms=sysl.get("uptime_ms"), reset_reason=sysl.get("reset_reason"))
    open(OUT / "live_session.jsonl", "a").write(json.dumps(rec) + "\n"); print(json.dumps(rec), flush=True); return rec
c = connect()
try:
    run(c, "RTL_UI OPEN DIGITAL_VOICE", 2.0); time.sleep(3)
    run(c, "RTL_DV SCAN STOP", 3.0); run(c, "RTL_DV MASK 1", 1.0); run(c, f"RTL_DV PLAN {plan}", 1.5)
    run(c, "RTL_DRIVER BIAS OFF", 1.0)
    print(run(c, "RTL_DV SCAN START", 2.0)[:2]); time.sleep(5)
    end = time.time() + minutes * 60; shot_done = False; last_q = 0
    while time.time() < end:
        try:
            r = snap(c, "live")
        except Exception as ex:   # serial hiccup (authorisation lapse, busy device): reconnect and carry on, the scan keeps running on the Tab5
            print('reconnect after', repr(ex)[:80], flush=True); time.sleep(8)
            try: c.close()
            except Exception: pass
            try: c = connect()
            except Exception: time.sleep(20); continue
            continue
        try: q = int(r.get("qual") or 0)
        except Exception: q = 0
        if q > last_q and not shot_done:
            subprocess.run([sys.executable, str(ROOT / "tools/tab5_png_screenshot.py"), "--port", "COM17", "--key", "F:/Ai/OrcSDR/.orclink/ui-doc.key", str(OUT / "live_first_voice.png")], capture_output=True, timeout=90)
            shot_done = True
        last_q = q
        time.sleep(12)
    run(c, "RTL_DV SCAN STOP", 3.0); snap(c, "end")
finally:
    c.close()
