"""MONITOR soak: select MONITOR ALL, start the scan, sample RTL_DV STATUS/UI every 2 s, report holds and voice per channel. Reopening COM17 resets the board once at start."""
import sys, time, collections
sys.path.insert(0, r"F:\Ai\OrcSDR\.claude\worktrees\dmr-digital-voice-review-dde01d\artifacts\digital-voice-integration\benchmarks")
from tab5_bench_lib import connect, run, find, kv
mins = float(sys.argv[1]) if len(sys.argv) > 1 else 10
c = connect(); time.sleep(2)
run(c, "RTL_UI OPEN DIGITAL_VOICE", 3); time.sleep(2)
print(find(run(c, "RTL_DV MONITOR ALL", 2), "RTL_DV_MONITOR"))
for _ in range(40):
    r = [l for l in run(c, "RTL_DV SCAN START", 2) if "RTL_DV_SCAN" in l]
    if r and "REFUSED" not in r[0]: break
    time.sleep(3)
print(r)
spk = collections.Counter(); last_sp = 0
t0 = time.time(); last_holds = 0; by = collections.Counter(); ev = {}; sec = collections.Counter(); prev = None
while time.time() - t0 < mins * 60:
    L = run(c, "RTL_DV STATUS", 1.0)
    st = kv(find(L, "RTL_DV_STATUS")); ui = kv(find(L, "RTL_DV_UI"))
    a = kv(find(run(c, "RTL_DV AUDIO", 0.6), "RTL_DV_AUDIO")); sp = int(a.get("speech", "0") or 0)
    ch = st.get("ch_hz"); holds = int(ui.get("holds", "0") or 0)
    if ch and ch != "0":
        sec[ch] += 2
        if holds > last_holds: by[ch] += holds - last_holds
        ev[ch] = (st.get("dmr_ev"), st.get("n48_ok"), st.get("n96_ok"))
    if sp > last_sp and ch: spk[ch] += sp - last_sp
    last_sp = max(last_sp, sp)
    last_holds = max(last_holds, holds)
    time.sleep(1)
print("minutes", round((time.time() - t0) / 60, 1), "total holds", last_holds)
for ch in sorted(sec, key=lambda k: -by[k]): print(ch, "holds", by[ch], "approx s", sec[ch], "dmr_ev/n48/n96 at last visit", ev.get(ch))
print("speech frames decoded by channel:", dict(spk) if spk else "none")
