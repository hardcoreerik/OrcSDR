"""Exercise the real Dial USB command parser; opens pairing but never tunes."""
import argparse
import time
import serial

parser = argparse.ArgumentParser()
parser.add_argument('--port', default='COM14')
args = parser.parse_args()
port = serial.Serial(port=None, baudrate=115200, timeout=0.2)
port.port = args.port
port.dtr = False
port.rts = False
port.open()
with port:
    def command(text, expected):
        port.write((text + '\n').encode())
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            line = port.readline().decode(errors='replace').strip()
            if line.startswith(expected):
                if expected.startswith('ORCDIAL_STATUS') and 'channel_valid=' not in line:
                    continue
                print(line)
                return line
        raise AssertionError('No response: ' + expected)

    command('ORCDIAL_STATUS', 'ORCDIAL_STATUS link=')
    command('x' * 80, 'ORCDIAL_COMMAND_ERROR too_long')
    command('ORCDIAL_NOT_A_COMMAND', 'ORCDIAL_COMMAND_ERROR unknown')
    command('ORCDIAL_PAIR START', 'ORCDIAL_PAIR_SEARCH_STARTED')
    status = command('ORCDIAL_STATUS', 'ORCDIAL_STATUS link=')
    assert 'pairing=1' in status or 'link=LINKED' in status, status
print('Dial serial smoke check passed')
