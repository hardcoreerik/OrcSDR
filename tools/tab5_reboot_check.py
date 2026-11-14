#!/usr/bin/env python3
"""Reset the Tab5 N times (RTS pulse on its serial port) and count, per boot, USB asserts, EP0 errors, whether the Pluto attached and the stream
started (RTL_START ESP_OK), and whether the boot-time or ladder power cycles fired. The first capture of a run has come back empty twice (cause not
found), so run N+1 and ignore it.

  python tools/tab5_reboot_check.py <boots> <seconds per boot> <log file>      (port is COM39; edit PORT below)

It resets the device: tell the owner first, and do not run it while a soak holds the port.
"""
PORT = 'COM39'
import serial, time, sys, os
N = int(sys.argv[1]); SECS = int(sys.argv[2]); out = sys.argv[3]
res = []
f = open(out, 'wb')
for i in range(N):
    s = serial.Serial(); s.port = PORT; s.baudrate = 115200; s.timeout = 0.5
    s.dtr = False; s.rts = True; s.open(); time.sleep(0.1); s.rts = False
    buf = b''; end = time.time() + SECS
    while time.time() < end:
        buf += s.read(4096)
    s.close()
    f.write(('\n=== BOOT %d ===\n' % (i + 1)).encode()); f.write(buf); f.flush()
    t = buf.decode('utf8', 'replace')
    res.append({'boot': i + 1, 'assert': t.count('assert failed'), 'attached': t.count('PlutoSDR attached'),
                'started': t.count('RTL_START ESP_OK'), 'boot_cycle': t.count('BOOT_RTL_RECOVERY'), 'ladder_cycle': t.count('PLUTO_POWER_CYCLE off'), 'ep0': t.count('EP 0 Error'), 'autogain': t.count('RTL_FM_AUTO_GAIN selected'),
                'skipped_gain_sel': [l for l in t.splitlines() if 'FM_AUTO_GAIN selected' in l][:1]})
    print(res[-1], flush=True)
print('SUMMARY asserts=%d of %d boots; started=%d' % (sum(r['assert'] for r in res), N, sum(1 for r in res if r['started'])), flush=True)
