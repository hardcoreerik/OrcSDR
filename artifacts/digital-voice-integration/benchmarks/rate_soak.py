"""10-minute soak at a fixed sample rate using RF Lab; samples the driver's own drop counters every 30 s."""
import json, sys, time
from tab5_bench_lib import connect, run, kv
RATE = int(sys.argv[1]) if len(sys.argv) > 1 else 2560000
MINUTES = float(sys.argv[2]) if len(sys.argv) > 2 else 10
def drv(c):
    for l in run(c, 'RTL_DRIVER STATUS', 1.5):
        if l.startswith('RTL_DRIVER_STATUS'): return kv(l)
    return {}
c = connect()
for l in run(c, 'RTL_LAB OPEN', 3.0): pass
run(c, 'RTL_LAB PAGE LIVE', 1.5)
r = run(c, f'RTL_LAB SET sample_rate_sps {RATE}', 4.0)
print([l[:100] for l in r if l.startswith('RTL_LAB')])
time.sleep(8)
samples = []; first = None; t_start = time.time()
while time.time() - t_start < MINUTES * 60:
    d = drv(c)
    if d:
        row = {'t': round(time.time() - t_start), 'state': d.get('state'), 'effective_sps': int(d.get('effective_sps', 0)),
               'bytes': int(d.get('bytes', 0)), 'overruns': int(d.get('overruns', 0)), 'drops': int(d.get('drops', 0))}
        if first is None: first = row
        samples.append(row)
        print(row, flush=True)
    time.sleep(28)
last = samples[-1]; dt = last['t'] - first['t']
summary = {'rate': RATE, 'seconds': dt, 'overruns': last['overruns'] - first['overruns'], 'drops': last['drops'] - first['drops'],
           'mb_per_s': round((last['bytes'] - first['bytes']) / dt / 1e6, 3), 'expected_mb_per_s': round(RATE * 2 / 1e6, 3),
           'min_effective_sps': min(s['effective_sps'] for s in samples), 'all_streaming': all(s['state'] == 'STREAMING' for s in samples)}
print('SUMMARY', summary)
run(c, 'RTL_LAB SET sample_rate_sps 2400000', 4.0)
open(f'rate_soak_{RATE}_2026-11-24.json', 'w').write(json.dumps({'summary': summary, 'samples': samples}, indent=1))
c.serial.close()
