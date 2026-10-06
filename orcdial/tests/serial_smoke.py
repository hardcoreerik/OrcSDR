"""Exercise the version-4 Dial USB parser without pairing, tuning or changing trust."""
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
        port.write(('\n' + text + '\n').encode())
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            line = port.readline().decode(errors='replace').strip()
            if line.startswith(expected):
                if expected.startswith('ORCDIAL_STATUS') and 'channel_valid=' not in line:
                    continue
                print(line)
                return line
        raise AssertionError('No response: ' + expected)

    security = command('ORCDIAL_STATUS', 'ORCDIAL_SECURITY ' )
    assert 'protocol=4' in security and 'trust=' in security and 'failure=' in security, security
    initial = command('ORCDIAL_STATUS', 'ORCDIAL_STATUS link=')
    if 'link=OFFLINE' in initial:
        command('ORCDIAL_ROTATE 1', 'ORCDIAL_CONTROL_ERROR offline')
        command('ORCDIAL_FOCUS NEXT', 'ORCDIAL_CONTROL_ERROR offline')
    command('x' * 160, 'ORCDIAL_COMMAND_ERROR too_long')
    command('ORCDIAL_NOT_A_COMMAND', 'ORCDIAL_COMMAND_ERROR unknown')
    command('ORCDIAL_ROTATE 999', 'ORCDIAL_CONTROL_ERROR invalid_delta')
    command('ORCDIAL_ROTATE 1junk', 'ORCDIAL_CONTROL_ERROR invalid_delta')
print('Dial version-4 read-only/parser smoke check passed')
