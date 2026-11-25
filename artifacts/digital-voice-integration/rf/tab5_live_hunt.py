"""Tab5-only live reception hunter v2. NO PC dongle and NO recorded IQ replay: the Tab5's own RTL-SDR (V4L + 140/440 antenna) is tuned over
serial (COM17), held on one channel at a time, and the receiver's own counters are logged. Serial authorisation is refreshed before every command
(it lapses while the radio runs). Stop: create tab5live/STOP. Log: tab5live/tab5live.jsonl (+ durable copy)."""
import os, sys, time, json, re
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "benchmarks"))
from tab5_bench_lib import connect, run, kv, find, dv
D = Path(__file__).parent / "tab5live"; D.mkdir(exist_ok=True)
import tab5_bench_lib as _lib
_orig_run = _lib.run
def _logged_run(c, cmd, window=1.2):
    lines = _orig_run(c, cmd, window)
    with open(D / "serial.log", "a", encoding="utf-8") as f:       # persistent serial monitor: every command and every line the Tab5 answered
        f.write(f"{time.strftime('%H:%M:%S')} > {cmd}{chr(10)}")
        for l in lines:
            if not l.startswith('{"type"'): f.write(f"  {l[:300]}{chr(10)}")
    return lines
run = _logged_run
ARCH = Path("F:/Ai/OrcSDR-data/digital-voice/tab5live"); ARCH.mkdir(parents=True, exist_ok=True)
SECS = float(sys.argv[1]) if len(sys.argv) > 1 else 60
# 2026-11-24: only the channels where voice/identities were actually heard on 2026-11-23 (PC hunter, outdoor antenna + LNA): Terra T3 152.1575, 152.685, 153.068
# gain 300 (30 dB) while the LNA is fitted: the old 49.6 dB was chosen without it
CH = [(152.1575, 300), (152.685, 300), (153.068, 300)]
# earlier list also had 152.345 (Terra Oakridge, never produced an identity) and 451.9125 (Capacity Max control, CRC-failing control traffic only)   # (MHz, manual gain tenth-dB or None = auto)
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
            run(c, "RTL_DV MASK 1", 1.2); run(c, "RTL_DV AUDIO SAVE ON", 1.2); run(c, "RTL_DV IQ ON", 1.2)   # DEV: 12 s IQ around call-like events, /orcsdr/dviq_*.cu8
            for mhz, gain in CH:
                if (D / "STOP").exists(): break
                run(c, "RTL_DV SCAN STOP", 4.0); run(c, f"RTL_DV PLAN {mhz}", 1.5); run(c, "RTL_DV MASK 1", 1.2); run(c, "RTL_DV SCAN START", 5.0); time.sleep(4)
                # The driver only accepts gain commands while the receiver is claimed, i.e. after the scan has started it.
                # Bias tee is the owner's setting (Home > BIAS); the firmware keeps it across scans. The hunter never touches it.
                if gain is None: run(c, "RTL_DRIVER GAINMODE AUTO", 1.5)
                else: run(c, "RTL_DRIVER GAINMODE MANUAL", 1.5); run(c, f"RTL_DRIVER GAIN {gain}", 1.5)
                run(c, "RTL_DV SCAN HOLD", 1.5)
                # Decoded DMR speech is saved by the firmware itself (SETUP > SAVE DMR SPEECH, persistent; RTL_DV AUDIO SAVE ON): /orcsdr/dmr_*.wav
                t0 = time.time(); first = None; last = {}; ui = ""; iqs = {}; bias_on = None
                while time.time() - t0 < SECS and not (D / "STOP").exists():
                    time.sleep(5); ui, r = dv(c)
                    aud = kv(find(run(c, "RTL_DV AUDIO STATUS", 1.0), "RTL_DV_AUDIO "))
                    iqs = kv(find(run(c, "RTL_DV IQ STATUS", 1.0), "RTL_DV_IQ "))
                    bias_l = next((l for l in run(c, "RTL_DRIVER STATUS", 1.5) if "bias=" in l), ""); bias_on = (re.search(r"bias=(\d)", bias_l) or [None, None])[1]
                    if r: last = r
                    if first is None and int(last.get("dmr_ev", 0)) > 0: first = round(time.time() - t0, 1)
                rec = dict(config=os.environ.get("DV_CONFIG", "Tab5 V4L + indoor 140/440 whip"), t=time.strftime("%Y-%m-%d %H:%M:%S"), mhz=mhz, gain=gain if gain else "auto", secs=round(time.time() - t0), source="tab5_live_antenna", replay=False,
                           link=last.get("link"), loss=last.get("loss"), level_milli=last.get("level_milli"), dmr_acq=last.get("dmr_acq"), dmr_ev=last.get("dmr_ev"),
                           dmr_id=last.get("dmr_id"), events=last.get("events"), first_dmr_event_s=first, hist=(re.search(r"hist=(\d+)", ui) or [None, None])[1],
                           bias_tee=bias_on,
                           iq={k: iqs.get(k) for k in ("triggers", "saved", "failed", "skipped_busy", "skipped_cooldown")},
                           audio={k: aud.get(k) for k in ("phase", "groups", "speech", "qual", "prot", "unq", "calls", "nfm_blocked", "rec", "rec_saved", "rec_discarded", "rec_busy")} if (aud := locals().get('aud')) else None,
                           db=(re.search(r"db_sys=\d+ db_tg=\d+ db_units=\d+", ui) or [None])[0])
                for p in (D / "tab5live.jsonl", ARCH / "tab5live.jsonl"):
                    with open(p, "a") as j: j.write(json.dumps(rec) + "\n")
                print(json.dumps(rec), flush=True)
            run(c, "RTL_DRIVER GAINMODE AUTO", 1.5)
        finally:
            c.close()
    except Exception as e:
        print("error", type(e).__name__, str(e)[:100], "line", e.__traceback__.tb_lineno, flush=True); time.sleep(15)
