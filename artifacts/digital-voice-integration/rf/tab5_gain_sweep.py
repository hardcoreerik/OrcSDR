"""Tab5-only gain experiment: for each gain, hold the channel and a quiet reference channel and log the receiver's own level and decoder counters.
usage: tab5_gain_sweep.py   (needs COM17; run when the hunter is stopped)"""
import sys, time, json, re
from pathlib import Path
sys.path.insert(0, "F:/Ai/OrcSDR/.claude/worktrees/dmr-digital-voice-review-dde01d/tools")
from help_media import Tab5
c = Tab5("COM17", Path("F:/Ai/OrcSDR/.orclink/ui-doc.key")); c.authenticate(create_key=False)
def kv(line): return {k: v for k, v in re.findall(r'(\w+)=("[^"]*"|\S+)', line)}
def ask(line, pre, t=8):
    c.send(line); return c.wait((pre,), t)
def status():
    c.send("RTL_DV STATUS"); c.wait(("RTL_DV_UI",), 8); return kv(c.wait(("RTL_DV_STATUS",), 8))
out = []
try:
    for _ in range(3):
        c.send("RTL_UI OPEN DIGITAL_VOICE"); c.wait(("RTL_UI",), 8); time.sleep(3)
        c.send("RTL_DV STATUS")
        if "open=1" in c.wait(("RTL_DV_UI",), 8): break
    c.wait(("RTL_DV_STATUS",), 8)
    ask("RTL_DV MASK 1", "RTL_DV_MASK")
    for g in ("auto", 150, 250, 350, 496):
        if g == "auto": ask("RTL_DRIVER GAINMODE AUTO", "RTL_DRIVER_RESULT")
        else:
            ask("RTL_DRIVER GAINMODE MANUAL", "RTL_DRIVER_RESULT"); ask(f"RTL_DRIVER GAIN {g}", "RTL_DRIVER_RESULT")
        for mhz, tag in ((152.1575, "terra"), (153.5, "quiet")):
            ask("RTL_DV SCAN STOP", "RTL_DV_SCAN"); ask(f"RTL_DV PLAN {mhz}:dmr", "RTL_DV_PLAN"); ask("RTL_DV SCAN START", "RTL_DV_SCAN"); time.sleep(2.5)
            ask("RTL_DV SCAN HOLD", "RTL_DV_SCAN"); time.sleep(30)
            st = status()
            rec = dict(gain=g, ch=tag, mhz=mhz, level_milli=st.get("level_milli"), dmr_acq=st.get("dmr_acq"), dmr_ev=st.get("dmr_ev"), dmr_id=st.get("dmr_id"), loss=st.get("loss"))
            print(json.dumps(rec), flush=True); out.append(rec)
finally:
    try: ask("RTL_DRIVER GAINMODE AUTO", "RTL_DRIVER_RESULT")
    except Exception: pass
    c.close()
    Path("tab5live").mkdir(exist_ok=True)
    Path("tab5live/gain_sweep_2026-11-23.json").write_text(json.dumps(out, indent=1))
