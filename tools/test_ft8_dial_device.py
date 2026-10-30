#!/usr/bin/env python3
"""Bounded live Tab5/Dial check; no reset, flash, synthetic RF, or pairing erasure.

Use --band explicitly to authorize tuning and a dashboard-command ack test.
Without it, observe the existing FT8 setup. Exit 0 PASS, 1 FAIL, 2 INCONCLUSIVE.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time


def fields(line):
    return dict(re.findall(r'(\w+)=("[^"]*"|\S+)', line))


def newer(candidate, previous):
    return candidate != 0 and (previous == 0 or 0 < ((candidate - previous) & 0xffffffff) < 0x80000000)


class Observation:
    """Only judge new records inside the measured window, not a cached station."""
    def __init__(self, baseline=0):
        self.baseline = baseline
        self.receiver = []
        self.dial = []
        self.slots = {}
        self.briefs = set()
        self.complete = set()
        self.errors = []

    def add(self, device, line):
        f = fields(line)
        if device == 'TAB5' and line.startswith('ORC_FT8_STATUS '):
            self.receiver.append(f)
        if device == 'TAB5' and line.startswith('ORC_FT8_RT slot='):
            self.slots[f['slot']] = int(f['decodes'])
        if device == 'DIAL' and line.startswith('ORCDIAL_STATUS link='):
            self.dial.append(f)
        if device == 'DIAL' and line.startswith('FT8_RX '):
            record = int(f['record'])
            if newer(record, self.baseline):
                if f['type'] == '17':
                    self.briefs.add(record)
                elif f['type'] == '18' and record in self.briefs:
                    self.complete.add(record)
        if device == 'TAB5' and line.startswith('RTL_ORCDIAL_STATUS ') and 'connection=Connected ' not in line:
            self.errors.append('receiver companion disconnected')
        if device == 'TAB5' and line.startswith('RTL_WIFI_C6_STATUS ') and f.get('transport') != '1':
            self.errors.append('receiver C6 transport offline')
        if line.startswith(('ACK_TIMEOUT', 'ACTION_REJECTED', 'ORCDIAL_CONTROL_ERROR',
                            'Guru Meditation', 'ESP-ROM:', 'Rebooting', 'ORC_FT8_ERROR')):
            self.errors.append(device + ': ' + line)

    def result(self):
        if self.errors or any(s.get('link') != 'LINKED' for s in self.dial):
            return 'FAIL', 'Link, command, or device error during observation'
        if not self.receiver or not self.dial:
            return 'FAIL', 'Missing receiver or Dial status evidence'
        if any(s.get('screen') != '1' or s.get('rx_running') != '1' or s.get('tap') != '1'
               or s.get('mode') != 'FT8' for s in self.receiver):
            return 'FAIL', 'FT8/IQ was not continuously active at the sampled checks'
        if len({s['dial_hz'] for s in self.receiver}) != 1 or any(s.get('dashboard') not in (None, '17') for s in self.dial):
            return 'FAIL', 'Band or dashboard changed during observation'
        if any(s.get('freq') != self.receiver[-1]['dial_hz'] for s in self.dial):
            return 'FAIL', 'Dial frequency did not match the receiver'
        if any(s.get('clock') != '1' for s in self.receiver):
            return 'INCONCLUSIVE', 'Clock was not valid throughout the observation'
        fresh = sum(self.slots.values())
        fresh = fresh or int(self.receiver[-1]['store']) > int(self.receiver[0]['store'])
        if not fresh:
            return 'INCONCLUSIVE', 'No new on-air decode observed; quiet RF is not a transport failure'
        if not self.complete:
            return 'FAIL', 'New receiver decodes observed, but no new complete Dial record arrived'
        return 'PASS', 'Fresh receiver decodes and new matching Dial summary/text records observed'


class Rig:
    def __init__(self, tab5, dial, log):
        self.ports = {'TAB5': tab5.s, 'DIAL': dial.s}
        self.log = log
        self.rows = []
        self.observation = None
        self.last_record = 0
        self.restore_link = False
        self.pending = {name: b"" for name in self.ports}

    def send(self, device, command):
        self.log.write(f'> {device} {command}\n')
        self.ports[device].write((command + '\n').encode('ascii'))

    def ingest(self, device, data):
        pending = self.pending[device] + data
        if len(pending) > 65536:
            raise RuntimeError(device + ': serial line exceeded 64 KiB')
        *complete, self.pending[device] = pending.split(b'\n')
        for raw in complete:
            line = raw.decode('utf-8', 'replace').strip()
            # Allowlist: never record serial pairing keys or AUTH proofs.
            if not line.startswith(('ORC_FT8', 'ORCDIAL_', 'RTL_ORCDIAL', 'RTL_WIFI_C6_STATUS', 'FT8_RX',
                                    'TX seq=', 'RX seq=', 'ACK_TIMEOUT', 'ACTION_REJECTED',
                                    'Guru Meditation', 'Rebooting', 'ESP-ROM:')):
                continue
            self.log.write(f'{datetime.now(timezone.utc).isoformat()} {device} {line}\n')
            self.log.flush()
            self.rows.append((device, line))
            if device == 'DIAL' and line.startswith('FT8_RX '):
                record = int(fields(line)['record'])
                if newer(record, self.last_record):
                    self.last_record = record
            if self.observation:
                self.observation.add(device, line)

    def pump(self, seconds):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            for device, port in self.ports.items():
                self.ingest(device, port.read(min(max(port.in_waiting, 1), 4096)))

    def query(self, device, command, prefix, timeout=8):
        start = len(self.rows)
        self.send(device, command)
        end = time.monotonic() + timeout
        next_poll = time.monotonic() + 1
        while time.monotonic() < end:
            self.pump(.15)
            for who, line in self.rows[start:]:
                if who == device and line.startswith(prefix):
                    return line
            if command in ('ORCDIAL_STATUS', 'RTL_ORCDIAL_STATUS', 'FT8 STATUS') and time.monotonic() >= next_poll:
                self.send(device, command)
                next_poll = time.monotonic() + 1
        raise TimeoutError(f'{device}: no {prefix} response to {command}')

    def linked(self):
        receiver = self.query('TAB5', 'RTL_ORCDIAL_STATUS', 'RTL_ORCDIAL_STATUS ')
        dial = self.query('DIAL', 'ORCDIAL_STATUS', 'ORCDIAL_STATUS link=LINKED pairing=')
        if fields(receiver).get('bridge') != '1' or 'trust=1 connection=Connected ' not in receiver or fields(dial).get('pending') != '0':
            raise RuntimeError('Existing pairing must be connected with no pending command')
        return fields(dial)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--tab5-port', required=True)
    ap.add_argument('--dial-port', required=True)
    ap.add_argument('--key', help='Existing Tab5 serial-auth key file; never printed')
    ap.add_argument('--antenna', required=True, help='Antenna, feedline and band suitability for this run')
    ap.add_argument('--dongle', default='not independently identified')
    ap.add_argument('--band', help='Explicitly select an FT8 band, e.g. 20m; otherwise no tuning commands')
    ap.add_argument('--seconds', type=float, default=120)
    ap.add_argument('--reconnect', action='store_true', help='Exercise Dial disconnect/connect; does not erase pairing')
    ap.add_argument('--png', action='store_true', help='Hash-verified FT8 SNAP before/after; no SD/stop fallback')
    ap.add_argument('--output', type=Path, help='New evidence directory; existing directories are refused')
    args = ap.parse_args()
    if args.tab5_port.casefold() == args.dial_port.casefold() or not 30 <= args.seconds <= 600:
        ap.error('Use distinct ports and an observation duration from 30 to 600 seconds')
    output = args.output or Path('.pio/ft8-dial-device') / datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    output.mkdir(parents=True, exist_ok=False)
    report = {'setup': {'tab5_port': args.tab5_port, 'dial_port': args.dial_port,
                        'antenna': args.antenna, 'dongle_user_reported': args.dongle,
                        'requested_band': args.band}, 'checks': {}, 'overall': 'FAIL',
              'physical_display_acceptance': 'NOT_RUN', 'injected': False,
              'runner_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    report['repository_head'] = subprocess.check_output(['git', '-C', str(Path(__file__).resolve().parent),
                                                        'rev-parse', 'HEAD'], text=True).strip()
    devices = []
    rig = None
    try:
        import tab5_ft8 as control
        from tab5_ft8_shot import snap_png
        key = control.find_key(args.key)
        for port in (args.tab5_port, args.dial_port):
            device = control.Tab5(port)
            devices.append(device)
            device.s.timeout = .02
        tab5, dial = devices
        tab5.authenticate(key)
        with (output / 'serial.log').open('w', encoding='utf-8') as log:
            rig = Rig(tab5, dial, log)
            c6 = rig.query('TAB5', 'RTL_WIFI_C6_STATUS', 'RTL_WIFI_C6_STATUS ')
            if fields(c6).get('transport') != '1':
                raise RuntimeError('Tab5 C6 transport is offline; no pairing/tuning commands sent')
            report['checks']['c6_transport'] = 'PASS'
            rig.linked()
            report['checks']['existing_pairing'] = 'PASS'
            if args.band:
                for command, prefix in [('FT8 MODE FT8', 'ORC_FT8_MODE_OK'),
                                        ('FT8 BAND ' + args.band, 'ORC_FT8_BAND_OK')]:
                    rig.query('TAB5', command, prefix)
                start = len(rig.rows)
                rig.send('DIAL', 'ORCDIAL_DASHBOARD 17')
                rig.pump(3)
                sent = [fields(line)['seq'] for who, line in rig.rows[start:]
                        if who == 'DIAL' and re.match(r'TX seq=\d+ type=15 ', line)]
                if len(sent) != 1 or rig.linked().get('ack') != sent[0]:
                    raise RuntimeError('Dashboard command acknowledgement did not match its TX sequence')
                report['checks']['control_ack'] = 'PASS'
            else:
                report['checks']['control_ack'] = 'NOT_RUN: --band is needed to authorize dashboard reopening/retune'
            if args.reconnect:
                before = rig.linked()
                rig.restore_link = True
                rig.send('DIAL', 'ORCDIAL_DISCONNECT')
                rig.query('DIAL', 'ORCDIAL_STATUS', 'ORCDIAL_SECURITY trust=1 connection=Disconnected ')
                rig.send('DIAL', 'ORCDIAL_CONNECT')
                deadline = time.monotonic() + 30
                while True:
                    try:
                        after = rig.linked()
                        break
                    except (TimeoutError, RuntimeError):
                        if time.monotonic() >= deadline:
                            raise
                if before['freq'] != after['freq'] or before['dashboard'] != after['dashboard']:
                    raise RuntimeError('Reconnection changed receiver frequency/dashboard')
                report['checks']['reconnect'] = 'PASS'
                rig.restore_link = False
            if args.png:
                snap_png(tab5, key, str(output / 'before.png'), tries=1)
            rig.pump(2)  # establish cached-record high water before measuring new delivery
            observation = Observation(rig.last_record)
            rig.observation = observation
            start = time.monotonic()
            while time.monotonic() - start < args.seconds:
                for device, command in [('TAB5', 'FT8 STATUS'), ('TAB5', 'RTL_ORCDIAL_STATUS'),
                                        ('TAB5', 'RTL_WIFI_C6_STATUS'), ('DIAL', 'ORCDIAL_STATUS')]:
                    rig.send(device, command)
                rig.pump(min(3, args.seconds - (time.monotonic() - start)))
            rig.query('TAB5', 'FT8 STATUS', 'ORC_FT8_STATUS ')
            rig.linked()
            rig.pump(3)  # allow the final decoded slot its bounded metadata retry interval
            rig.observation = None
            result, reason = observation.result()
            report.update(overall=result, reason=reason, elapsed_s=round(time.monotonic() - start, 2),
                          baseline_record=observation.baseline, complete_record_ids=sorted(observation.complete),
                          fresh_slots=observation.slots, receiver_samples=observation.receiver,
                          errors=observation.errors)
            report['checks']['live_delivery'] = result
            if args.png:
                snap_png(tab5, key, str(output / 'after.png'), tries=1)
                status = fields(rig.query('TAB5', 'FT8 STATUS', 'ORC_FT8_STATUS '))
                if status.get('rx_running') != '1' or status.get('tap') != '1':
                    raise RuntimeError('FT8/IQ not active after PNG capture')
                report['checks']['png_transfer'] = 'PASS: both PNGs length/hash verified; visual acceptance not automated'
    except (Exception, SystemExit) as error:
        report.update(overall='FAIL', error=str(error))
        if rig:
            report['last_c6_status'] = next((s for who, s in reversed(rig.rows)
                                            if who == 'TAB5' and s.startswith('RTL_WIFI_C6_STATUS ')), None)
    finally:
        if rig and rig.restore_link:
            # The evidence log may already be closed by exception unwinding.
            with (output / 'serial.log').open('a', encoding='utf-8') as log:
                rig.log = log
                rig.observation = None
                try:
                    rig.send('TAB5', 'RTL_ORCDIAL_CONNECT')
                    rig.send('DIAL', 'ORCDIAL_CONNECT')
                    rig.linked()
                    report['cleanup'] = 'Paired link restored after interrupted reconnect test'
                except (Exception, SystemExit) as error:
                    report['cleanup'] = 'Link recovery not confirmed: ' + str(error)
        for device in devices:
            device.s.close()
        report['artifacts'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                               for p in output.iterdir() if p.is_file()}
        (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f"{report['overall']}: {report.get('error', report.get('reason', ''))}")
    print(f'Evidence: {output.resolve()}')
    return {'PASS': 0, 'FAIL': 1, 'INCONCLUSIVE': 2}[report['overall']]


if __name__ == '__main__':
    raise SystemExit(main())
