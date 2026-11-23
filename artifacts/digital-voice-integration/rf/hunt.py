"""Unattended live-DMR hunter: PC RTL-SDR Blog V4 + LNA (bias tee on). Cycles candidate channels, captures CU8 at
240 kS/s (channel +60 kHz above centre), replays through the production dv_radio chain (DMR only) in WSL, keeps captures
that produced integrity-valid DMR events. Stop: create hunt/STOP. Log: hunt/hunt.log, hunt/results.jsonl."""
import subprocess, time, json, re, os, sys
from pathlib import Path
H = Path(__file__).parent / "hunt"; H.mkdir(exist_ok=True)
CH = [(442962500,"PNW DMR Eugene CC1 (RRDB)"),(442887500,"PNW DMR Salem CC1"),(442750000,"PNW DMR Colton CC1"),(442812500,"PNW DMR Mt Hebo CC1"),(444025000,"PNW DMR Buxton CC9"),(451912500,"TRBOWest CapMax Blanton Ridge ctrl"),(442425000,"ham CC1 (unverified)"),(152157500,"Terra T3 ctrl"),(152345000,"Terra Oakridge"),(152106000,"candidate voice 152.106"),(152219000,"candidate voice 152.219"),(152685000,"candidate 152.685"),(152895000,"candidate 152.895"),(153068000,"candidate 153.068"),(441744000,"sweep 441.744"),(441676000,"sweep 441.676"),(441050000,"sweep 441.050"),(431998000,"sweep 431.998"),(438026000,"sweep 438.026")]
SECS = 14
WSL_BIN = "/mnt/f/Ai/OrcSDR/.claude/worktrees/dmr-digital-voice-review-dde01d/build/dv-live-rel/dv_replay"
DSD = Path(r'C:/Users/hardc/AppData/Local/Temp/claude/d15739b94789b7e4/f1e7b073-d808-4cda-94b9-310faed44d50/scratchpad/dsdfme/x/dsd-fme-portable')
CALL = re.compile(r'SRC:|TGT:|Source:|Target:|VLC|TLC|Voice LC|GRANT|C_AHOY|_MOVE|UU_V|Group Voice|Private Voice|Group Call|Private Call')
def dsd_calls(f):
    """Third-party cross-check: DSD-FME call/grant keywords (control-only broadcasts do not match)."""
    w = f.with_suffix('.wav')
    try:
        subprocess.run(['python', str(Path(__file__).parent/'fm_wav.py'), str(f), str(w)], capture_output=True, timeout=60)
        env = dict(os.environ, PATH=str(DSD/'dsd-fme')+os.pathsep+os.environ['PATH'])
        p = subprocess.run([str(DSD/'dsd-fme'/'dsd-fme.exe'), '-i', str(w), '-fa', '-Z', '-o', 'null'], capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=60, cwd=str(DSD), env=env)
        txt = p.stderr + p.stdout
    except Exception:
        return -1, ''
    finally:
        w.unlink(missing_ok=True)
    return len([l for l in txt.splitlines() if CALL.search(l)]), txt
def log(m):
    s = time.strftime("%H:%M:%S ") + m; print(s, flush=True)
    with open(H/"hunt.log","a") as f: f.write(s+"\n")
last_keep = {}
while not (H/"STOP").exists():
    for hz, name in CH:
        if (H/"STOP").exists(): break
        try:
            subprocess.run(["rtl_biast","-d","0","-b","1"],capture_output=True)
            f = H/f"cap_{hz}_{int(time.time())}.cu8"
            r = subprocess.run(["rtl_sdr","-f",str(hz-60000),"-s","240000","-g","30","-n",str(240000*SECS),str(f)],capture_output=True,timeout=60)
            if not f.exists() or f.stat().st_size < 1000: log(f"{hz} capture failed"); continue
            wp = "/mnt/"+str(f).replace("\\","/").replace(":","",1).lower() if False else None
            drive = str(f.resolve()).replace("\\","/"); wp = "/mnt/"+drive[0].lower()+drive[2:]
            p = subprocess.run(["wsl","-e",WSL_BIN,wp,str(hz),"1"],capture_output=True,text=True,timeout=45)
            m = re.search(r"events=(\d+).*?dmr_acq=(\d+) dmr_ev=(\d+) dmr_id=(\d+)",p.stdout)
            lm = re.search(r"level_milli=(\d+)",p.stdout)
            ev,acq,dev,did = (int(x) for x in m.groups()) if m else (-1,0,0,0)
            rec = dict(t=time.strftime("%Y-%m-%d %H:%M:%S"),hz=hz,name=name,events=ev,dmr_ev=dev,dmr_id=did,level=int(lm.group(1)) if lm else None,file=f.name)
            keep = ev < 0 or did > 0 or (ev > 0 and time.time()-last_keep.get(hz,0) > 3600)
            if keep: last_keep[hz] = time.time()
            dn = -2
            if ev > 0 or (rec['level'] or 0) >= 50:
                r2 = dsd_calls(f); dn = r2[0]
                if dn > 0:
                    keep = True; (H/(f.stem+'.dsd.txt')).write_text(r2[1])
            rec['dsd_call_lines'] = dn
            rec["kept"] = keep
            with open(H/"results.jsonl","a") as j: j.write(json.dumps(rec)+"\n")
            log(f"{hz/1e6:.4f} {name}: events={ev} dmr_ev={dev} id={did} level={rec['level']} dsd={dn} kept={keep}")
            if not keep: f.unlink(missing_ok=True)
            pend = sorted(H.glob('cap_*.cu8'), key=lambda q: q.stat().st_mtime)
            while len(pend) > 120: pend.pop(0).unlink(missing_ok=True)
        except Exception as e:
            log(f"{hz} error {type(e).__name__}: {str(e)[:80]}"); time.sleep(5)
log("stopped")
