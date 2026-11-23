"""Exact-line diagnostic response reader; public health ignores stale auth lines."""
import time


def read_response(serial_port, prefix, timeout=8, public=False):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        raw = serial_port.readline()
        if not raw:
            continue
        line = raw.decode('utf-8', 'replace').strip()
        if line.startswith(prefix):
            return line
        failures = ('RTL_IQ_ERROR ', 'RTL_IQ_GET_ERROR ', 'ESP-ROM:',
                    'Guru Meditation', 'assert failed')
        if line.startswith(failures) or (not public and line.startswith('AUTH_REQUIRED')):
            raise RuntimeError('Diagnostic response rejected or device restarted')
    raise TimeoutError('Missing exact response: ' + prefix)


def self_check():
    class Fake:
        def __init__(self, lines):
            self.lines = iter(lines)
        def readline(self):
            return next(self.lines, b'')
    health = b'RTL_HEALTH_STATUS uptime_ms=123\r\n'
    assert read_response(Fake([b'AUTH_REQUIRED\n', b'echo RTL_HEALTH_STATUS\n', health]),
                         'RTL_HEALTH_STATUS ', public=True).endswith('123')
    try:
        read_response(Fake([b'AUTH_REQUIRED\n']), 'RTL_DRIVER_STATUS ')
    except RuntimeError:
        pass
    else:
        raise AssertionError('Protected responses must reject auth failure')
    try:
        read_response(Fake([b'Guru Meditation Error\n']), 'RTL_HEALTH_STATUS ', public=True)
    except RuntimeError:
        pass
    else:
        raise AssertionError('Public health must detect a crash')
    print('PASS exact prefix, stale public auth, protected auth and crash handling')


if __name__ == '__main__':
    self_check()
