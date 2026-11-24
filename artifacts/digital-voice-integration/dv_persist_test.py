"""On-device persistence check: learn a recorded call into the discovery DB (owner opt-in), save, close, reopen, confirm the table came back from SD."""
import sys, time, re
from pathlib import Path
sys.path.insert(0, "F:/Ai/OrcSDR/.claude/worktrees/dmr-digital-voice-review-dde01d/tools")
from help_media import Tab5
c = Tab5("COM17", Path("F:/Ai/OrcSDR/.orclink/ui-doc.key")); c.authenticate(create_key=False)
def say(cmd, pre, t=10):
    c.send(cmd); r = c.wait((pre,), t); print(cmd, "->", r[:260]); return r
def status():
    c.send("RTL_DV STATUS"); u = c.wait(("RTL_DV_UI",), 8); c.wait(("RTL_DV_STATUS",), 8); return u
def open_dash():
    for _ in range(4):
        c.send("RTL_UI OPEN DIGITAL_VOICE"); c.wait(("RTL_UI",), 8); time.sleep(4)
        u = status()
        if "open=1" in u: return u
    raise SystemExit("could not open")
try:
    print(open_dash()[:200])
    say("RTL_DV DB CLEAR", "RTL_DV_DB")
    say("RTL_DV DB LEARN_REPLAY ON", "RTL_DV_DB")
    say("RTL_DV MASK 1", "RTL_DV_MASK")
    say("RTL_DV REPLAY /orcsdr/dv_live_call_152p1575.cu8 152157500", "RTL_DV_REPLAY")
    time.sleep(22)
    print("after replay:", re.search(r"db_sys=.*", status()).group(0))
    say("RTL_DV DB SAVE", "RTL_DV_DB"); time.sleep(4)
    print("after save:", re.search(r"db_sys=.*", status()).group(0))
    c.send("RTL_STOP"); time.sleep(2)
    say("RTL_UI OPEN HOME", "RTL_UI") if False else None
finally:
    c.close()
