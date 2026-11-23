import sys, hashlib, time
sys.path.insert(0, "F:/Ai/OrcSDR/.claude/worktrees/dmr-digital-voice-review-dde01d/tools")
from help_media import Tab5
from pathlib import Path
local, remote = Path(sys.argv[1]), sys.argv[2]
data = local.read_bytes(); sha = hashlib.sha256(data).hexdigest()
c = Tab5("COM17", Path("F:/Ai/OrcSDR/.orclink/ui-doc.key"))
try:
    c.authenticate(create_key=False)
    c.send("SD_PUT_ABORT")
    try: print(c.wait(("SD_PUT",), 3))
    except TimeoutError: pass
    c.send("RTL_STOP")
    try: c.wait(("RTL_STOPPING",), 3)
    except TimeoutError: pass
    time.sleep(1.5)
    r = ""
    for _ in range(30):
        c.send(f"SD_PUT_BEGIN {len(data)} {sha} {remote.encode().hex()}")
        r = c.wait(("SD_PUT_READY", "SD_PUT_ERROR"), 10); print(r)
        if r.startswith("SD_PUT_READY"): break
        if "radio_busy" not in r: sys.exit(1)
        time.sleep(0.5)
    if not r.startswith("SD_PUT_READY"): sys.exit(1)
    t0 = time.time(); off = 0
    while off < len(data):
        n = min(16384, len(data) - off)
        c.send(f"SD_PUT_CHUNK {n}")
        d = c.wait(("SD_PUT_DATA", "SD_PUT_ERROR"), 20)
        if d.startswith("SD_PUT_ERROR"): print(d); sys.exit(1)
        c.serial.write(data[off:off+n]); c.serial.flush()
        a = c.wait(("SD_PUT_ACK", "SD_PUT_ERROR"), 30)
        if a.startswith("SD_PUT_ERROR"): print(a); sys.exit(1)
        off += n
        if (off // 16384) % 50 == 0: print(f"  {off/1e6:.1f}/{len(data)/1e6:.1f} MB {off/1e3/(time.time()-t0):.0f} kB/s", flush=True)
    print(c.wait(("SD_PUT_DONE", "SD_PUT_COMMIT", "SD_PUT_ERROR", "SD_PUT_OK"), 60))
finally:
    c.close()
