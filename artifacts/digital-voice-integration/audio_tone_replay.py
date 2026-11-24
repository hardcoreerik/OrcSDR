"""Stage C (speaker path with known-good PCM) and Stage D (SD replay of recorded IQ -> native speech -> speaker) on the Tab5, COM17.
Counts speaker chunks through RTL_WIFI_COEX_STATUS (audio_chunks/audio_drops) and the voice engine through RTL_DV AUDIO STATUS."""
import json, re, sys, time
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "artifacts/digital-voice-integration/benchmarks"))
from tab5_bench_lib import connect, run, find, kv
OUT = Path(__file__).parent / "audio"; OUT.mkdir(exist_ok=True)
def st(c):
    a = kv(find(run(c, "RTL_DV AUDIO STATUS", 1.2), "RTL_DV_AUDIO "))
    s = kv(find(run(c, "RTL_WIFI_COEX_STATUS", 1.0), "RTL_WIFI_COEX_STATUS"))
    d = kv(find(run(c, "RTL_DV STATUS", 1.0), "RTL_DV_STATUS"))
    return a, s, d
def rec(tag, a, s, d):
    keep = {k: a.get(k) for k in ("buffer_ms", "gap_ms", "phase", "listen", "slot", "groups", "frames", "speech", "silence", "errors", "concealed", "underruns", "overruns", "flushes",
                                  "synth_us", "buffered_ms", "peak_ms", "latency_ms", "rms", "peak", "clipped", "test_samples", "nfm_blocked", "q_pending", "q_drops",
                                  "qual", "prot", "unq", "calls", "pumps", "pump_max_us", "stack_hwm")}
    keep.update(audio_chunks=s.get("audio_chunks"), audio_drops=s.get("audio_drops"), speaker_running=s.get("speaker_running"), audio_enabled=s.get("audio_enabled"))
    keep.update(worker_busy_pm=d.get("worker_busy_pm"), ring_drops=d.get("ring_drops"), loss=d.get("loss"), dec_internal=d.get("dec_internal"), ring_max=d.get("ring_max"))
    line = dict(tag=tag, t=time.strftime("%H:%M:%S"), **keep)
    open(OUT / "tone_replay.jsonl", "a").write(json.dumps(line) + "\n"); print(json.dumps(line), flush=True)
mode = sys.argv[1] if len(sys.argv) > 1 else "tone"
c = connect()
try:
    run(c, "RTL_UI OPEN DIGITAL_VOICE", 2.0); time.sleep(3)
    if mode == "tone":
        rec("before_tone", *st(c))
        print(run(c, "RTL_DV AUDIO TONE 800", 1.5)[-1:] )
        time.sleep(1.2); rec("tone_playing", *st(c)); time.sleep(2.5); rec("after_tone", *st(c))
    elif mode == "replay":
        sd = sys.argv[2]; hz = sys.argv[3]
        if len(sys.argv) > 4: print(run(c, f"RTL_DV AUDIO BUFFER {sys.argv[4]}", 1.2)[-3:])
        run(c, "RTL_DV SCAN STOP", 3.0); run(c, "RTL_DV MASK 1", 1.0)
        rec("before_replay", *st(c)); t0 = time.time()
        print(run(c, f"RTL_DV REPLAY {sd} {hz}", 2.0)[-2:])
        while time.time() - t0 < 70:
            time.sleep(4); rec("replay_%02d" % int(time.time() - t0), *st(c))
            a, s, d = st(c)
            if int(d.get("pairs", 0)) >= 3360000 and int(d.get("ring_pend", 1)) == 0: break
        time.sleep(6); rec("replay_end", *st(c))
finally:
    c.close()
