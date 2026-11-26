"""Poll the decode worker's load while a scan runs: worker_busy_pm (per mille of wall time), ring drops, state placement and free internal RAM."""
import sys,time,re
sys.path.insert(0,r"F:\Ai\OrcSDR\.claude\worktrees\dmr-digital-voice-review-dde01d\artifacts\digital-voice-integration\benchmarks")
from tab5_bench_lib import connect, run, find, kv
mins=float(sys.argv[1]) if len(sys.argv)>1 else 6
c=None
for _ in range(15):
    try: c=connect(); break
    except Exception: time.sleep(3)
t0=time.time()
while time.time()-t0<mins*60:
    try:
        st=kv(find(run(c,"RTL_DV STATUS",1.2),"RTL_DV_STATUS"))
        m=find(run(c,"RTL_MEM STATUS",1.2),"RTL_MEM")
        fi=re.findall(r"(internal_free=\S+|intern_free=\S+|dma_largest=\S+|int_free=\S+)",m)
        print("t=%4.0f busy_pm=%s ring_drops=%s ring_max=%s dec_internal=%s blk_us_avg=%s %s"%(time.time()-t0,st.get("worker_busy_pm"),st.get("ring_drops"),st.get("ring_max"),st.get("dec_internal"),st.get("blk_us_avg"),fi[:3]),flush=True)
    except Exception as e:
        print("err",str(e)[:60],flush=True)
        try: c=connect()
        except Exception: pass
    time.sleep(15)
