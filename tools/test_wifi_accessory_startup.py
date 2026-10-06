"""Explicit hardware regression: resets Tab5; tests boot Wi-Fi on/off with SDR attached.

Uses the existing USB pairing key. Never flashes, forgets trust or changes credentials.
"""
import argparse
import json
import re
import time
from datetime import datetime, timezone
from pathlib import Path

from help_media import Tab5


def fields(line):
    return dict(re.findall(r'(\w+)=([^ ]+)', line))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM17')
    parser.add_argument('--pairing-key', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--pair-search', action='store_true', help='Start/cancel accessory discovery after each boot; never approve trust.')
    parser.add_argument('--restore-boot', type=int, choices=(0, 1), help='Restore a previously recorded boot preference instead of the current one.')
    args = parser.parse_args()
    if not args.pairing_key.exists():
        parser.error('An existing USB pairing key is required.')
    args.output.mkdir(parents=True, exist_ok=False)
    tab = Tab5(args.port, args.pairing_key)
    result = {'utc': datetime.now(timezone.utc).isoformat(), 'cases': [], 'result': 'FAIL'}
    log = (args.output / 'serial.log').open('w', encoding='utf-8')

    def receive_line():
        line = tab.serial.readline().decode(errors='replace').strip()
        if line:
            log.write(line + '\n')
            log.flush()
            if ('sdmmc_send_cmd returned' in line or 'Guru Meditation' in line or
                    'Backtrace:' in line or 'request: no response' in line):
                raise AssertionError('Transport/panic failure: ' + line)
        return line

    def wait(prefixes, seconds=20):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            line = receive_line()
            if not line:
                continue
            if line.startswith(prefixes):
                return line
        raise TimeoutError('Missing response: ' + str(prefixes))

    def command(text, prefixes, seconds=20):
        log.write('> ' + text + '\n')
        tab.send(text)
        return wait(prefixes, seconds)

    original = None
    try:
        tab.authenticate()
        status = fields(command('RTL_WIFI_STATUS', ('RTL_WIFI_STATUS ',)))
        assert status['power'] == '1', 'Wi-Fi power must be enabled before this test.'
        original = args.restore_boot if args.restore_boot is not None else int(status['auto_connect'])
        for boot in (0, 1):
            case = {'boot_connect': boot, 'result': 'FAIL'}
            result['cases'].append(case)
            print('Testing boot Wi-Fi=' + str(boot), flush=True)
            command(f'RTL_UI ACTION SETTINGS WIFI_BOOT {boot}', ('RTL_UI_ACTION_OK',))
            command('RTL_RESET', ('RTL_RESETTING',))
            wait(('SPLASH_BOOT done',), 90)
            # Allow staged accessory startup while the real receiver runs.
            end = time.monotonic() + 5
            while time.monotonic() < end:
                receive_line()
            tab.authenticate()
            if args.pair_search:
                # Mutations are queued without an immediate serial acknowledgment.
                tab.send('RTL_ORCDIAL_PAIR START')
                command('RTL_ORCDIAL_STATUS', ('RTL_ORCDIAL_STATUS ',))
            command('RTL_WIFI_SCAN', ('RTL_WIFI_SCAN_QUEUED',))
            scan = command('RTL_WIFI_STATUS', ('RTL_WIFI_SCAN_RESULTS count=',), 30)
            case['networks'] = int(fields(scan)['count'])
            assert case['networks'] > 0, 'No networks found in the known-router setup.'
            command('RTL_WIFI_CONNECT_SAVED', ('RTL_WIFI_CONNECT_QUEUED',))
            deadline = time.monotonic() + 40
            while time.monotonic() < deadline:
                state = fields(command('RTL_WIFI_STATUS', ('RTL_WIFI_STATUS ',)))
                if state['connected'] == '1':
                    break
                time.sleep(1)
            else:
                raise AssertionError('Saved network did not connect.')
            case['channel'] = command('RTL_WIFI_CHANNEL', ('RTL_WIFI_CHANNEL ',))
            coex = command('RTL_WIFI_COEX_STATUS', ('RTL_WIFI_COEX_STATUS ',))
            case['receiver'] = coex
            assert fields(coex)['rtl_ready'] == '1', 'Attached receiver was not ready.'
            if args.pair_search:
                tab.send('RTL_ORCDIAL_PAIR CANCEL')
                command('RTL_ORCDIAL_STATUS', ('RTL_ORCDIAL_STATUS ',))
            case['result'] = 'PASS'
            print('PASS boot Wi-Fi=' + str(boot), flush=True)
        result['result'] = 'PASS'
    except (AssertionError, TimeoutError, RuntimeError, OSError) as error:
        result['failure'] = str(error)
        print(str(error), flush=True)
    finally:
        try:
            if original is not None:
                tab.authenticate()
                tab.send(f'RTL_UI ACTION SETTINGS WIFI_BOOT {original}')
                result['restored_boot'] = original if tab.wait(('RTL_UI_ACTION_OK',), 15) == 'RTL_UI_ACTION_OK' else None
        except (RuntimeError, TimeoutError, OSError) as error:
            result['restore_error'] = str(error)
            result['result'] = 'FAIL'
        tab.close()
        log.close()
        (args.output / 'report.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    return 0 if result['result'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
