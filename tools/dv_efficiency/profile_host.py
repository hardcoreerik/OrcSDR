#!/usr/bin/env python3
"""Host-only baseline/candidate evidence. Run on a clean disposable checkout.
No hardware or serial access. No IQ, WAV or frame dumps in published reports.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import statistics
import subprocess
import tempfile
import time

def command(argv, root, log=None):
    start=time.monotonic()
    before=resource.getrusage(resource.RUSAGE_CHILDREN)
    p=subprocess.run([str(a) for a in argv],cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    after=resource.getrusage(resource.RUSAGE_CHILDREN)
    if log is not None:
        log.write_text(p.stdout)
    if p.returncode:
        print(p.stdout)
        raise RuntimeError(f"failed {argv}: {p.returncode}")
    return p.stdout, (after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime)*1000, (time.monotonic()-start)*1000

def sha(b): return hashlib.sha256(b).hexdigest()

def main():
    a=argparse.ArgumentParser()
    a.add_argument("--root",type=Path,default=Path.cwd())
    a.add_argument("--output",type=Path,required=True)
    a.add_argument("--compare",type=Path)
    o=a.parse_args();root=o.root.resolve();out=o.output.resolve();out.mkdir(parents=True,exist_ok=True)
    report={"revision":command(["git","rev-parse","HEAD"],root)[0].strip(),"compiler":command(["c++","--version"],root)[0].splitlines()[0],"cases":[]}
    with tempfile.TemporaryDirectory(prefix="dv-eff-") as tmp:
        tmp=Path(tmp);build=tmp/"live";ui=tmp/"ui"
        # Use host wall timing for stage accounting; keep deterministic simulated
        # now_ms for protocol timing. Expand event trace only in the test harness.
        bench=root/"tests/digital_voice_live/dv_bench.cpp"
        replay=root/"tests/digital_voice_live/dv_replay.cpp"
        old=bench.read_bytes();oldr=replay.read_bytes()
        b=old.decode();r=oldr.decode()
        assert "static uint32_t us() { return g_now * 1000u; }" in b
        b=b.replace("#include <cmath>","#include <cmath>\n#include <chrono>")
        b=b.replace("static uint32_t us() { return g_now * 1000u; }","static uint32_t us() { return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()); }")
        b=b.replace("    dv_radio::stop();","    char line[1024]; dv_radio::format_status(line,sizeof(line)); std::fprintf(stderr,\"%s\\n\",line);\n    dv_radio::stop();")
        assert "if (shown++ < 40)" in r
        r=r.replace("if (shown++ < 40)","if (++shown)")
        try:
            bench.write_text(b);replay.write_text(r)
            command(["cmake","-S",root/"tests/digital_voice_live","-B",build,"-DCMAKE_BUILD_TYPE=Release"],root,out/"configure.log")
            command(["cmake","--build",build,"-j2"],root,out/"build.log")
            command(["ctest","--test-dir",build,"--output-on-failure"],root,out/"live-tests.log")
            command(["cmake","-S",root/"tests/digital_voice_dashboard_preview","-B",ui,"-DCMAKE_BUILD_TYPE=Release"],root,out/"ui-configure.log")
            command(["cmake","--build",ui,"-j2"],root,out/"ui-build.log")
            command(["ctest","--test-dir",ui,"--output-on-failure"],root,out/"ui-tests.log")
            corpus=root/"tests/digital_voice_corpus"
            for ln in (corpus/"manifest.txt").read_text().splitlines():
                if not ln.startswith("case "):continue
                _,name,file,hz,mask,*_=ln.split()
                path=corpus/file
                trace,_,_=command([build/"dv_replay",path,hz,mask],root)
                # Each printed event, including sequence and stream time, plus count.
                ev="\n".join(x for x in trace.splitlines() if x.startswith("  t=") or x.startswith("shown "))
                result={"case":name,"iq_sha256":sha(path.read_bytes()),"events_sha256":sha(ev.encode()),"events":sum(x.startswith("  t=") for x in trace.splitlines())}
                if int(mask)==1:
                    voice=tmp/name;voice.mkdir()
                    v,_,_=command([build/"dv_voice_replay",path,hz,voice,name],root)
                    result["voice"]={p.name:{"bytes":p.stat().st_size,"sha256":sha(p.read_bytes())} for p in sorted(voice.iterdir())}
                    result["voice_counters"]=[x.split(" synth_us")[0] for x in v.splitlines() if "engine groups=" in x]+[x for x in v.splitlines() if x.startswith("  call ") or x.startswith("  speaker ")]
                    result["synthesis"]=[x.split("synth_us ")[1].split(" latency_ms")[0] for x in v.splitlines() if "synth_us " in x]
                cpus=[];walls=[];stages=[]
                for repeat in range(3):
                    s,c,w=command([build/"dv_bench","file",path,hz,mask],root);cpus.append(c);walls.append(w)
                    stages.append({k:int(v) for word in s.split() if "=" in word for k,v in [word.split("=",1)] if k in ("pairs","us_ch","us_dmr","us_n48","us_n96") and v.isdigit()})
                result["host_cpu_ms"]=cpus;result["host_wall_ms"]=walls;result["stage_wall_us"]=stages
                report["cases"].append(result)
            for mask in (1,2,4,7):
                cpus=[];walls=[];stages=[];summary=None
                for repeat in range(3):
                    s,c,w=command([build/"dv_bench","noise","3",mask,"25","42"],root);cpus.append(c);walls.append(w)
                    stages.append({k:int(v) for word in s.split() if "=" in word for k,v in [word.split("=",1)] if k in ("pairs","us_ch","us_dmr","us_n48","us_n96") and v.isdigit()})
                    summary=json.loads(next(x for x in s.splitlines() if x.startswith("{")))
                report["cases"].append({"case":f"noise_mask_{mask}","summary":summary,"host_cpu_ms":cpus,"host_wall_ms":walls,"stage_wall_us":stages})
        finally:
            bench.write_bytes(old);replay.write_bytes(oldr)
    (out/"report.json").write_text(json.dumps(report,indent=2)+"\n")
    if o.compare:
        base=json.loads(o.compare.read_text())
        assert [x["case"] for x in base["cases"]]==[x["case"] for x in report["cases"]]
        for x,y in zip(base["cases"],report["cases"]):
            for key in ("iq_sha256","events_sha256","events","voice","voice_counters","summary"):
                assert x.get(key)==y.get(key),f"{x['case']}: {key} differs"
            print(x["case"],"cpu ms median baseline/candidate",statistics.median(x["host_cpu_ms"]),statistics.median(y["host_cpu_ms"]))
        print("PASS: ordered printed events, corpus IQ, native AMBE wire traces and both PCM WAV outputs byte-identical; noise result summaries identical")
    print(json.dumps(report,indent=2))
if __name__=="__main__": main()
