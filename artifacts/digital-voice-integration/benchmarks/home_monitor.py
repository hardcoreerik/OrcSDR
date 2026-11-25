"""Read-only watch of the driver's counters while Home runs normally (nothing is commanded)."""
import json, sys, time
from tab5_bench_lib import connect, run, kv
MINUTES = float(sys.argv[1]) if len(sys.argv) > 1 else 10
def drv(c):
    for l in run(c, 'RTL_DRIVER STATUS', 1.5):
        if l.startswith('RTL_DRIVER_STATUS'): return kv(l)
    return {}
c = connect(); t0 = time.time(); rows = []
while time.time() - t0 < MINUTES * 60:
    d = drv(c)
    if d:
        r = {'t': round(time.time() - t0), 'state': d.get('state'), 'eff': int(d.get('effective_sps', 0)), 'bytes': int(d.get('bytes', 0)),
             'overruns': int(d.get('overruns', 0)), 'drops': int(d.get('drops', 0)), 'bias': d.get('bias')}
        rows.append(r); print(r, flush=True)
    time.sleep(27)
a, b = rows[0], rows[-1]; dt = b['t'] - a['t']
s = {'seconds': dt, 'mb_per_s': round((b['bytes'] - a['bytes']) / dt / 1e6, 3), 'expected_at_2560k': 5.12,
     'overruns': b['overruns'] - a['overruns'], 'drops': b['drops'] - a['drops'], 'all_streaming': all(r['state'] == 'STREAMING' for r in rows)}
print('SUMMARY', s, flush=True)
open('home_monitor_2026-11-24.json', 'w').write(json.dumps({'summary': s, 'rows': rows}, indent=1))
c.serial.close()
