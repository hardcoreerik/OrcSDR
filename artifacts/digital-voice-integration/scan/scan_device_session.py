"""Physical SCAN-screen session on the Tab5 (COM17): drives the real touch handlers with RTL_UI TAP, captures PNGs with the existing
tools/tab5_png_screenshot.py (FT8 SNAP; no documentation mode), and records render-time / IQ-continuity counters from RTL_DV STATUS."""
import json, re, subprocess, sys, time
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "artifacts/digital-voice-integration/benchmarks"))
from tab5_bench_lib import connect, run, find, kv, dv, mem
OUT = Path(__file__).parent / "png"
LOG = Path(__file__).parent / "device_session.jsonl"
def shot(name):
    r = subprocess.run([sys.executable, str(ROOT / "tools/tab5_png_screenshot.py"), "--port", "COM17", "--key", "F:/Ai/OrcSDR/.orclink/ui-doc.key", str(OUT / name)],
                       capture_output=True, text=True, timeout=90)
    print(name, r.stdout.strip()[-60:], r.stderr.strip()[-80:], flush=True)
def stats(c, tag):
    u, r = dv(c); m = mem(c)
    d = dict(tag=tag, t=time.strftime("%H:%M:%S"), render=re.search(r"render_n=\d+ render_avg_us=\d+ render_max_us=\d+", u).group(0) if re.search(r"render_n=", u) else None,
             scan=re.search(r"scan=(.*?) plan=", u).group(1), stage=re.search(r"stage=\d+", u).group(0), opt=re.search(r"opt=\d+/\d+", u).group(0),
             loss=r.get("loss"), gaps=r.get("gaps"), ring_drops=r.get("ring_drops"), blk_us_avg=r.get("blk_us_avg"), worker_busy_pm=r.get("worker_busy_pm"),
             psram_free=m.get("psram_free"), internal_free=m.get("internal_free"))
    open(LOG, "a").write(json.dumps(d) + "\n"); print(json.dumps(d), flush=True); return d
def tap(c, x, y, wait=1.2):
    run(c, f"RTL_UI TAP {x} {y}", wait)
def ensure_open(c):
    for _ in range(4):
        run(c, "RTL_UI OPEN DIGITAL_VOICE", 2.0); time.sleep(3); u, _ = dv(c)
        if "open=1" in u: return True
    return False
START = (129, 599); HOLD = (288, 599); SKIP = (415, 599); SAVE = (532, 599); PREV = (654, 599); NEXT = (762, 599)
step = sys.argv[1] if len(sys.argv) > 1 else "all"
c = connect()
try:
    if not ensure_open(c): raise SystemExit("cannot open dashboard")
    run(c, "RTL_DV SCAN STOP", 3.0); run(c, "RTL_DV MASK 1", 1.0)
    stats(c, "open")
finally: c.close()
shot("01_scan_ready")
c = connect()
try:
    tap(c, *NEXT); stats(c, "plan_next_1")
finally: c.close()
shot("02_plan_2m_amateur_selected")
c = connect()
try:
    tap(c, *START, wait=3.0); time.sleep(20); stats(c, "range_scanning_20s")
finally: c.close()
shot("03_range_scanning")
c = connect()
try:
    tap(c, *START, wait=4.0); run(c, "RTL_DV SCAN STOP", 3.0)
    run(c, "RTL_DV PLAN 152.1575:dmr,152.685:dmr,152.345:dmr,153.068:dmr", 1.5); run(c, "RTL_DV MASK 1", 1.0)
    stats(c, "list_ready")
finally: c.close()
shot("04_channel_list_ready")
c = connect()
try:
    tap(c, *START, wait=3.0); time.sleep(75); stats(c, "list_scanning_75s")
finally: c.close()
shot("05_channel_list_scanning")
c = connect()
try:
    tap(c, *HOLD, wait=1.5); time.sleep(6); stats(c, "hold")
finally: c.close()
shot("06_hold")
c = connect()
try:
    tap(c, *HOLD, wait=1.5); run(c, "RTL_DV SCAN PAUSE", 1.5); time.sleep(2); stats(c, "paused")
finally: c.close()
shot("07_paused")
c = connect()
try:
    run(c, "RTL_DV SCAN RESUME", 1.5); time.sleep(40); stats(c, "resumed_40s")
    run(c, "RTL_DV SCAN STOP", 4.0); stats(c, "stopped")
finally: c.close()
shot("08_stopped_after_scan")
