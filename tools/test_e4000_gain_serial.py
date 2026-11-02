#!/usr/bin/env python3
"""E4000 gain API smoke test through one persistent Python serial connection.

Offline: --self-check. Hardware: --run --port COM39 --key KEYFILE --output NEW.json.
Requires an already-streaming gain-capable E4000. Does not tune, start, flash,
request reset or control an RF generator. This tests controls, not calibrated RF gain.
"""
import argparse
import json
from pathlib import Path
import re
import time

EXPECTED_STEPS = [-10, 15, 40, 65, 90, 115, 140, 165, 190, 215, 240, 290, 340, 420]


def fields(line):
    return {k: v.strip('"') for k, v in re.findall(r'(\w+)=("[^"]*"|\S+)', line)}


def self_check():
    value = fields('RTL_DRIVER_STATUS state=STREAMING mode=MANUAL gain_tenth_db=-10 bytes=42')
    assert int(value['gain_tenth_db']) == -10 and int(value['bytes']) == 42
    identity = fields('RTL_GAIN_STATUS receiver="Nooelec XTR" steps=-10,15,40')
    assert identity['receiver'] == 'Nooelec XTR'
    assert [int(x) for x in identity['steps'].split(',')] == [-10, 15, 40]
    assert len(EXPECTED_STEPS) == 14 and EXPECTED_STEPS[0] == -10
    print('PASS signed gain, quoted receiver and ladder parsers; no device accessed')


def run(args):
    from tab5_ft8 import Tab5, find_key
    output = Path(args.output)
    if output.exists():
        raise RuntimeError('Choose a new output file; existing evidence is preserved')
    output.parent.mkdir(parents=True, exist_ok=True)
    result = {'scope': 'serial gain controls; no RF-amplitude acceptance', 'events': [], 'pass': False}
    dev = None
    initial = initial_gain = None
    baseline_uptime = None

    def save():
        output.write_text(json.dumps(result, indent=2) + '\n')

    def query(command, prefix):
        dev.send(command)
        lines = dev.read_until((prefix, 'AUTH_REQUIRED', 'ESP-ROM:', 'Guru Meditation', 'assert failed'), 6, echo=False)
        matches = [line for line in lines if line.startswith(prefix)]
        if not matches:
            raise RuntimeError('Missing response or reset/authentication error for ' + command)
        line = matches[-1]
        result['events'].append({'t': time.time(), 'command': command, 'response': line})
        save()
        return fields(line)

    def command(text):
        reply = query(text, 'RTL_DRIVER_RESULT ')
        if reply.get('accepted') != '1' or reply.get('result') != 'ESP_OK':
            raise RuntimeError('Control rejected: ' + text)

    def settled(mode, gain=None):
        started = time.monotonic()
        first_bytes = None
        while time.monotonic() - started < 6:
            value = query('RTL_DRIVER STATUS', 'RTL_DRIVER_STATUS ')
            if value.get('state') != 'STREAMING':
                raise RuntimeError('Receiver left STREAMING during gain test')
            count = int(value['bytes'])
            if first_bytes is None:
                first_bytes = count
            if (value.get('mode') == mode and (gain is None or int(value['gain_tenth_db']) == gain)
                    and count > first_bytes and time.monotonic() - started >= .3):
                return value
            time.sleep(.15)
        raise RuntimeError('Applied gain/mode and continuing delivery did not settle')

    try:
        dev = Tab5(args.port)
        dev.send('')  # Finish any partial console line left by the prior client.
        health = query('RTL_HEALTH', 'RTL_HEALTH_STATUS ')
        baseline_uptime = int(health['uptime_ms'])
        if baseline_uptime < 30000:
            raise RuntimeError('Device recently restarted; wait for stable reception before this test')
        dev.authenticate(find_key(args.key))
        initial = query('RTL_DRIVER STATUS', 'RTL_DRIVER_STATUS ')
        if initial.get('profile') != '5' or initial.get('state') != 'STREAMING':
            raise RuntimeError('An already-streaming E4000 is required')
        if initial.get('gain_cap') != '1' or initial.get('gain_auto_cap') != '1':
            initial = None  # Old firmware: preflight only, no restore/control requests.
            raise RuntimeError('Firmware does not advertise E4000 manual/AUTO gain')
        initial_gain = query('RTL_GAIN STATUS', 'RTL_GAIN_STATUS ')
        steps = [int(x) for x in initial_gain['steps'].split(',')]
        if steps != EXPECTED_STEPS:
            raise RuntimeError('Returned gain ladder differs from captured E4000 reference')
        for gain in steps:
            command('RTL_DRIVER GAIN ' + str(gain))
            settled('MANUAL', gain)
        command('RTL_DRIVER GAINMODE AUTO')
        settled('AUTO')
        health = query('RTL_HEALTH', 'RTL_HEALTH_STATUS ')
        if int(health['uptime_ms']) <= baseline_uptime:
            raise RuntimeError('Device uptime did not advance')
        final = query('RTL_DRIVER STATUS', 'RTL_DRIVER_STATUS ')
        for counter in ('overruns', 'drops'):
            if int(final[counter]) != int(initial[counter]):
                raise RuntimeError('Reported loss increased: ' + counter)
        if final['frequency_hz'] != initial['frequency_hz']:
            raise RuntimeError('Receiver frequency changed during the gain test')
        result['pass'] = True
    except BaseException as error:
        result['error'] = str(error)
    finally:
        if dev is not None:
            if initial is not None and initial_gain is not None:
                try:
                    command('RTL_DRIVER GAIN ' + initial['gain_tenth_db'])
                    settled('MANUAL', int(initial['gain_tenth_db']))
                    if initial_gain.get('mode') == 'SMART' and initial_gain.get('band') in ('FM', 'AM'):
                        query('RTL_UI ACTION ' + initial_gain['band'] + ' GAIN_AUTO', 'RTL_UI_ACTION_OK')
                        restored = query('RTL_GAIN STATUS', 'RTL_GAIN_STATUS ')
                        if restored.get('mode') != 'SMART':
                            raise RuntimeError('Initial SMART controller did not restore')
                    elif initial.get('mode') == 'AUTO':
                        command('RTL_DRIVER GAINMODE AUTO')
                        settled('AUTO')
                    result['restore'] = 'requested initial gain/controller restored'
                except BaseException as error:
                    result['restore_error'] = str(error)
                    result['pass'] = False
            dev.s.close()
        save()
    if not result['pass']:
        raise SystemExit('Gain test did not pass; see ' + str(output))
    print('PASS 14 gain controls and tuner AUTO; RF-amplitude acceptance remains separate: ' + str(output))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--self-check', action='store_true')
    mode.add_argument('--run', action='store_true')
    parser.add_argument('--port', default='COM39')
    parser.add_argument('--key')
    parser.add_argument('--output')
    args = parser.parse_args()
    if args.self_check:
        self_check()
    elif not args.output:
        parser.error('--run requires --output NEW.json')
    else:
        run(args)
