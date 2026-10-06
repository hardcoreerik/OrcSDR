"""Read Tab5 version-4 status and Wi-Fi channel; never opens pairing or changes trust."""
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
    status = command('RTL_ORCDIAL_STATUS', 'RTL_ORCDIAL_STATUS ')
    assert 'protocol=4' in status and 'trust=' in status and 'connection=' in status and 'failure=' in status, status
print('Tab5 read-only status check passed; this does not prove RF delivery or pairing')
