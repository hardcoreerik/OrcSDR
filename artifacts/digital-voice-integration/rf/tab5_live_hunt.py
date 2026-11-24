"""Tab5-only live reception hunter v2. NO PC dongle and NO recorded IQ replay: the Tab5's own RTL-SDR (V4L + 140/440 antenna) is tuned over
serial (COM17), held on one channel at a time, and the receiver's own counters are logged. Serial authorisation is refreshed before every command
(it lapses while the radio runs). Stop: create tab5live/STOP. Log: tab5live/tab5live.jsonl (+ durable copy)."""
import sys, time, json, re
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "benchmarks"))
from tab5_bench_lib import connect, run, kv, find, dv
D = Path(__file__).parent / "tab5live"; D.mkdir(exist_ok=True)
ARCH = Path("F:/Ai/OrcSDR-data/digital-voice/tab5live"); ARCH.mkdir(parents=True, exist_ok=True)
SECS = float(sys.argv[1]) if len(sys.argv) > 1 else 60
CH = [(152.1575, 496), (152.685, 496), (152.345, 496), (451.9125, None), (153.068, 496)]   # (MHz, manual gain tenth-dB or None = auto)
def open_dash(c):
    for _ in range(4):
        run(c, "RTL_UI OPEN DIGITAL_VOICE", 2.0); time.sleep(3)
        u, r = dv(c)
        if "open=1" in u: return True
    return False
while not (D / "STOP").exists():
    try:
        c = connect()
        try:
            if not open_dash(c): time.sleep(20); continue
            run(c, "RTL_DV MASK 1", 1.2)
            for mhz, gain in CH:
                if (D / "STOP").exists(): break
                if gain is None: run(c, "RTL_DRIVER GAINMODE AUTO", 1.5)
                else: run(c, "RTL_DRIVER GAINMODE MANUAL", 1.5); run(c, f"RTL_DRIVER GAIN {gain}", 1.5)
                run(c, "RTL_DV SCAN STOP", 4.0); run(c, f"RTL_DV PLAN {mhz}", 1.5); run(c, "RTL_DV MASK 1", 1.2); run(c, "RTL_DV SCAN START", 5.0); time.sleep(4)
                run(c, "RTL_DV SCAN HOLD", 1.5)
                t0 = time.time(); first = None; last = {}; ui = ""
                while time.time() - t0 < SECS and not (D / "STOP").exists():
                    time.sleep(5); ui, r = dv(c)
                    if r: last = r
                    if first is None and int(last.get("dmr_ev", 0)) > 0: first = round(time.time() - t0, 1)
                rec = dict(t=time.strftime("%Y-%m-%d %H:%M:%S"), mhz=mhz, gain=gain if gain else "auto", secs=round(time.time() - t0), source="tab5_live_antenna", replay=False,
                           link=last.get("link"), loss=last.get("loss"), level_milli=last.get("level_milli"), dmr_acq=last.get("dmr_acq"), dmr_ev=last.get("dmr_ev"),
                           dmr_id=last.get("dmr_id"), events=last.get("events"), first_dmr_event_s=first, hist=(re.search(r"hist=(\d+)", ui) or [None, None])[1],
                           db=(re.search(r"db_sys=\d+ db_tg=\d+ db_units=\d+", ui) or [None])[0])
                for p in (D / "tab5live.jsonl", ARCH / "tab5live.jsonl"):
                    with open(p, "a") as j: j.write(json.dumps(rec) + "\n")
                print(json.dumps(rec), flush=True)
            run(c, "RTL_DRIVER GAINMODE AUTO", 1.5)
        finally:
            c.close()
    except Exception as e:
        print("error", type(e).__name__, str(e)[:100], "line", e.__traceback__.tb_lineno, flush=True); time.sleep(15)
