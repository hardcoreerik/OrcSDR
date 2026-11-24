"""One replay of a recorded real call on the Tab5, then a real framebuffer capture of every Digital Voice view (replay-labelled)."""
import sys, time, re
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent / "benchmarks"))
from tab5_bench_lib import connect, run, find, dv
OUT = Path(__file__).parent / "captures"
c = connect()
try:
    for _ in range(4):
        run(c, "UI_DOC_SHOW digital_voice.scan live", 3.0); time.sleep(3); u, r = dv(c)
        if "open=1" in u: break
    run(c, "RTL_DV DB LEARN_REPLAY OFF", 1.0); run(c, "RTL_DV MASK 1", 1.0)
    print(run(c, "RTL_DV REPLAY /orcsdr/dv_live_call_152p1575.cu8 152157500", 1.5)[0][:80])
    time.sleep(40)
    for _ in range(40):
        time.sleep(3); u, r = dv(c)
        if r and int(r.get("events", 0)) >= 186: break
    for view in ("scan", "live", "heard", "systems", "signal", "setup"):
        run(c, f"UI_DOC_SHOW digital_voice.{view} live", 3.0); time.sleep(2.5)
        L = run(c, f"UI_CAPTURE dv_screen_{view}", 6.0)
        done = find(L, "UI_CAPTURE_DONE"); print(view, done[:60])
        if done:
            remote = re.search(r'path="([^"]+)"', done).group(1)
            c.authenticate(create_key=False); c.get_file(remote, OUT / f"dv_screen_{view}.bmp")
finally:
    c.close()
