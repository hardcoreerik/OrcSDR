"""Check the Tab5 USB pairing/probe commands without updating the C6 or tuning."""
import argparse
import re
import time
import serial

parser = argparse.ArgumentParser()
parser.add_argument('--port', default='COM17')
args = parser.parse_args()
port = serial.Serial(port=None, baudrate=115200, timeout=0.2)
port.port = args.port
port.dtr = False
port.rts = False
port.open()
with port:
    def command(text, expected):
        port.write(('\n' + text + '\n').encode())
        deadline = time.monotonic() + 12
        while time.monotonic() < deadline:
            line = port.readline().decode(errors='replace').strip()
            if line.startswith(expected):
                print(line)
                return line
        raise AssertionError('No response: ' + expected)

    command('RTL_WIFI_C6_STATUS', 'RTL_WIFI_C6_STATUS ')
    channel = command('RTL_WIFI_CHANNEL', 'RTL_WIFI_CHANNEL primary=')
    assert re.search(r'primary=\d+ secondary=\d+ connected=[01] ssid_hex=[0-9a-fA-F]* ap_primary=\d+', channel), channel
    command('RTL_ORCDIAL_PAIR START', 'RTL_ORCDIAL_PAIR_WINDOW_OPEN seconds=60')
    result = command('RTL_ORCDIAL_PROBE', 'RTL_ORCDIAL_PROBE result=')
    assert re.search(r'result=[A-Z0-9_]+ scope=relay_request$', result), result
    status = command('RTL_ORCDIAL_STATUS', 'RTL_ORCDIAL_STATUS ')
    assert 'pairing=1' in status or 'paired=1' in status, status
print('Host command smoke check passed; relay result above does not prove pairing')
