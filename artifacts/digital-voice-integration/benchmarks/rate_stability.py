"""Does the Tab5 hold a sample rate without dropping samples? Uses RF Lab to set the rate and the driver's own counters."""
import re, time, json, sys
from tab5_bench_lib import connect, run, kv, find
def drv(c):
    for l in run(c, 'RTL_DRIVER STATUS', 1.5):
        if l.startswith('RTL_DRIVER_STATUS'): return kv(l)
    return {}
def snap(d): return {k: d.get(k) for k in ('state','effective_sps','bytes','blocks','overruns','drops')}
c = connect()
out = {'t': time.strftime('%Y-%m-%d %H:%M:%S')}
print('baseline', snap(drv(c)))
for l in run(c, 'RTL_LAB OPEN', 3.0):
    if l.startswith('RTL_LAB'): print(l[:160])
res = {}
for rate in (2400000, 2560000):
    r = run(c, f'RTL_LAB SET sample_rate_sps {rate}', 4.0)
    for l in r:
        if l.startswith('RTL_LAB'): print(l[:160])
    time.sleep(6)                      # let the restart settle
    a = drv(c); t0 = time.time()
    time.sleep(40)
    b = drv(c); dt = time.time() - t0
    def d(k): return int(b.get(k, 0)) - int(a.get(k, 0))
    res[rate] = {'seconds': round(dt, 1), 'state': b.get('state'), 'effective_sps': b.get('effective_sps'),
                 'bytes_delta': d('bytes'), 'blocks_delta': d('blocks'), 'overruns_delta': d('overruns'), 'drops_delta': d('drops'),
                 'overruns_total': b.get('overruns'), 'drops_total': b.get('drops'),
                 'mb_per_s': round(d('bytes') / dt / 1e6, 3), 'expected_mb_per_s': round(rate * 2 / 1e6, 3)}
    print(rate, res[rate])
# put the rate back to Home's default
for l in run(c, 'RTL_LAB SET sample_rate_sps 2400000', 4.0):
    if l.startswith('RTL_LAB'): print(l[:160])
out['results'] = res
open('rate_stability_2026-11-24.json', 'w').write(json.dumps(out, indent=1))
c.serial.close()
