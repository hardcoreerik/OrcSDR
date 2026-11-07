"""Host checks for the PCF8563 register rules and time conversion. No I2C hardware."""
import calendar
import ctypes as c
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Time(c.Structure):
    _fields_ = [('year', c.c_uint16), ('month', c.c_uint8), ('day', c.c_uint8), ('weekday', c.c_uint8),
                ('hour', c.c_uint8), ('minute', c.c_uint8), ('second', c.c_uint8), ('low_voltage', c.c_bool)]


class ProtocolTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temp = tempfile.TemporaryDirectory(); cls.addClassCleanup(temp.cleanup)
        d = Path(temp.name)
        inc = ROOT / 'ports/p4/components/orcsdr_board_hosyond/include'
        (d / 'wrapper.c').write_text(
            '#include "pcf8563_protocol.h"\n'
            '__declspec(dllexport) int dec(const uint8_t*p,size_t n,pcf8563_time_t*t){return pcf8563_decode(p,n,t);}\n'
            '__declspec(dllexport) void enc(const pcf8563_time_t*t,uint8_t*o){pcf8563_encode(t,o);}\n'
            '__declspec(dllexport) int from_epoch(uint32_t e,pcf8563_time_t*t){return pcf8563_from_epoch(e,t);}\n'
            '__declspec(dllexport) int to_epoch(const pcf8563_time_t*t,uint32_t*e){return pcf8563_to_epoch(t,e);}\n')
        cl = Path(r'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.50.35717/bin/Hostx64/x64/cl.exe')
        ucrt = sorted(Path(r'C:/Program Files (x86)/Windows Kits/10/Include').glob('*/ucrt'))[-1]
        env = dict(os.environ); env['PATH'] = str(cl.parent) + os.pathsep + env['PATH']
        run = subprocess.run([str(cl), '/nologo', '/LD', '/O2', '/W4', '/WX', '/GS-', '/Zl', '/I' + str(cl.parents[3] / 'include'),
                              '/I' + str(ucrt), '/I' + str(inc), 'wrapper.c', '/link', '/NOENTRY', '/NODEFAULTLIB',
                              '/OUT:pcf.dll'], cwd=d, env=env, text=True, capture_output=True)
        if run.returncode: raise AssertionError(run.stdout + run.stderr)
        cls.lib = c.CDLL(str(d / 'pcf.dll'))
        kernel = c.WinDLL('kernel32'); kernel.FreeLibrary.argtypes = [c.c_void_p]
        cls.addClassCleanup(lambda: kernel.FreeLibrary(cls.lib._handle))
        cls.lib.dec.argtypes = [c.c_void_p, c.c_size_t, c.POINTER(Time)]
        cls.lib.enc.argtypes = [c.POINTER(Time), c.c_void_p]
        cls.lib.from_epoch.argtypes = [c.c_uint32, c.POINTER(Time)]
        cls.lib.to_epoch.argtypes = [c.POINTER(Time), c.POINTER(c.c_uint32)]

    def decode(self, raw):
        t = Time()
        ok = self.lib.dec(bytes(raw), len(raw), c.byref(t))
        return ok, t

    def test_decode_reads_bcd_fields_and_ignores_unused_bits(self):
        # 2026-11-07 13:45:09, Saturday (6); unused high bits set on minutes/hours/day/month-low-bits are masked.
        raw = [0x09, 0x45, 0x13, 0x07, 0x06, 0x11, 0x26]
        ok, t = self.decode(raw)
        self.assertTrue(ok)
        self.assertEqual((t.year, t.month, t.day, t.weekday, t.hour, t.minute, t.second, t.low_voltage),
                         (2026, 11, 7, 6, 13, 45, 9, False))
        masked = [raw[0], raw[1] | 0x80, raw[2] | 0xC0, raw[3] | 0xC0, raw[4] | 0xF8, raw[5] | 0x60, raw[6]]
        self.assertTrue(self.decode(masked)[0])

    def test_vl_flag_is_reported_not_fatal(self):
        ok, t = self.decode([0x89, 0x45, 0x13, 0x07, 0x06, 0x11, 0x26])
        self.assertTrue(ok); self.assertTrue(t.low_voltage); self.assertEqual(t.second, 9)

    def test_decode_rejects_bad_bcd_ranges_and_dates(self):
        good = [0x09, 0x45, 0x13, 0x07, 0x06, 0x11, 0x26]
        for index, value in ((0, 0x60), (0, 0x0A), (1, 0x60), (2, 0x24), (3, 0x00), (3, 0x32), (4, 0x07), (5, 0x00),
                             (5, 0x13), (6, 0xA0)):
            bad = list(good); bad[index] = value
            self.assertFalse(self.decode(bad)[0], (index, value))
        self.assertFalse(self.decode([0x00, 0x00, 0x00, 0x31, 0x00, 0x04, 0x26])[0])   # 31 April
        self.assertFalse(self.decode([0x00, 0x00, 0x00, 0x29, 0x00, 0x02, 0x25])[0])   # 29 Feb, not a leap year
        self.assertTrue(self.decode([0x00, 0x00, 0x00, 0x29, 0x00, 0x02, 0x28])[0])     # 29 Feb 2028
        self.assertTrue(self.decode([0x00, 0x00, 0x00, 0x29, 0x00, 0x02, 0x00])[0])     # 29 Feb 2000 (divisible by 400)
        self.assertFalse(self.decode(good[:6])[0])
        self.assertFalse(self.decode(good + [0])[0])

    def test_century_bit_set_is_rejected(self):
        self.assertFalse(self.decode([0x09, 0x45, 0x13, 0x07, 0x06, 0x91, 0x26])[0])

    def test_epoch_round_trip_across_the_accepted_window(self):
        for epoch in (1704067200, 1709164799, 1709164800, 1767225599, 1767225600, 1832198400, 2000000000, 4102444799):
            t = Time()
            self.assertTrue(self.lib.from_epoch(epoch, c.byref(t)), epoch)
            ref = calendar.timegm((t.year, t.month, t.day, t.hour, t.minute, t.second))
            self.assertEqual(ref, epoch)
            self.assertEqual(t.weekday, (calendar.weekday(t.year, t.month, t.day) + 1) % 7, epoch)  # python: Mon=0
            back = c.c_uint32()
            self.assertTrue(self.lib.to_epoch(c.byref(t), c.byref(back)))
            self.assertEqual(back.value, epoch)

    def test_encode_decode_round_trip_and_vl_cleared(self):
        t = Time()
        self.assertTrue(self.lib.from_epoch(1794058309, c.byref(t)))
        t.low_voltage = True
        out = (c.c_uint8 * 7)()
        self.lib.enc(c.byref(t), out)
        self.assertEqual(out[0] & 0x80, 0)
        self.assertEqual(out[5] & 0x80, 0)
        ok, back = self.decode(list(out))
        self.assertTrue(ok)
        self.assertFalse(back.low_voltage)
        e = c.c_uint32()
        self.assertTrue(self.lib.to_epoch(c.byref(back), c.byref(e)))
        self.assertEqual(e.value, 1794058309)

    def test_epoch_outside_window_is_refused(self):
        t = Time()
        self.assertFalse(self.lib.from_epoch(1704067199, c.byref(t)))
        self.assertFalse(self.lib.from_epoch(4102444800, c.byref(t)))
        early = Time(year=2023, month=12, day=31, hour=23, minute=59, second=59)
        self.assertFalse(self.lib.to_epoch(c.byref(early), c.byref(c.c_uint32())))


if __name__ == '__main__': unittest.main()
