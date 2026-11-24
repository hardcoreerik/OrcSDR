"""On-device native voice audio checks (COM17, M5Stack Tab5 dev unit). Receive only. No TX. Steps: open Digital Voice, STATUS, SELFTEST,
speaker-path TONE, SD replay of a recorded call (Stage D), screenshots through the existing PNG tool. usage: audio_device_session.py [step...]"""
import json, re, subprocess, sys, time
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "artifacts/digital-voice-integration/benchmarks"))
from tab5_bench_lib import connect, run, find, kv, dv, mem
OUT = Path(__file__).parent / "audio"; OUT.mkdir(exist_ok=True)
LOG = OUT / "device_session.jsonl"
def shot(name):
    r = subprocess.run([sys.executable, str(ROOT / "tools/tab5_png_screenshot.py"), "--port", "COM17", "--key", "F:/Ai/OrcSDR/.orclink/ui-doc.key", str(OUT / name)],
                       capture_output=True, text=True, timeout=90)
    print(name, r.stdout.strip()[-60:], r.stderr.strip()[-80:], flush=True)
def log(tag, text):
    if isinstance(text, list): text = " || ".join(l for l in text if l.startswith(("RTL_DV", "ORC_SUPPORT", "RTL_SPEAKER", "RTL_UI")) )
    open(LOG, "a").write(json.dumps(dict(tag=tag, t=time.strftime("%H:%M:%S"), text=text)) + "\n"); print(tag, "::", text, flush=True)
def ensure_open(c):
    for _ in range(4):
        run(c, "RTL_UI OPEN DIGITAL_VOICE", 2.0); time.sleep(3); u, _ = dv(c)
        if "open=1" in u: return True
    return False
steps = sys.argv[1:] or ["open", "status", "selftest", "tone"]
c = connect()
try:
    if "open" in steps:
        log("open", str(ensure_open(c)))
    if "status" in steps:
        log("status", run(c, "RTL_DV AUDIO STATUS", 1.5)); log("dvstatus", run(c, "RTL_DV STATUS", 1.5)); log("mem", str(mem(c)))
    if "selftest" in steps:
        log("selftest", run(c, "RTL_DV AUDIO SELFTEST", 4.0))
    if "tone" in steps:
        log("tone", run(c, "RTL_DV AUDIO TONE 600", 1.5)); time.sleep(2); log("status_after_tone", run(c, "RTL_DV AUDIO STATUS", 1.5))
finally:
    c.close()
