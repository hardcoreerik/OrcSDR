"""Host checks for the GT9895 packet rules (MIT-derived, see gt9895_protocol.h). No I2C hardware."""
import ctypes as c
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
NONE, RELEASE, CONTACTS, IGNORE, INVALID = range(5)


class ProtocolTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temp = tempfile.TemporaryDirectory(); cls.addClassCleanup(temp.cleanup)
        d = Path(temp.name)
        src = ROOT / 'ports/p4/components/orcsdr_board_hosyond/src'
        (d / 'wrapper.c').write_text(
            '#include "gt9895_protocol.h"\n'
            '__declspec(dllexport) int fw(const uint8_t*p,size_t n){return gt9895_firmware_ok(p,n);}\n'
            '__declspec(dllexport) int head(const uint8_t*p,size_t n,unsigned*c){return (int)gt9895_parse_header(p,n,c);}\n'
            '__declspec(dllexport) int contact(const uint8_t*p,size_t n,unsigned k,int32_t*x,int32_t*y){return gt9895_parse_first_contact(p,n,k,x,y);}\n'
            '__declspec(dllexport) int scale(int32_t r,int32_t a,int32_t b){return gt9895_scale(r,a,b);}\n'
            '__declspec(dllexport) void addr(uint32_t a,uint8_t*o){gt9895_address_bytes(a,o);}\n')
        cl = Path(r'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.50.35717/bin/Hostx64/x64/cl.exe')
        ucrt = sorted(Path(r'C:/Program Files (x86)/Windows Kits/10/Include').glob('*/ucrt'))[-1]
        env = dict(os.environ); env['PATH'] = str(cl.parent) + os.pathsep + env['PATH']
        run = subprocess.run([str(cl), '/nologo', '/LD', '/O2', '/GS-', '/Zl', '/I' + str(cl.parents[3] / 'include'),
                              '/I' + str(ucrt), '/I' + str(src), 'wrapper.c', '/link', '/NOENTRY', '/NODEFAULTLIB',
                              '/OUT:protocol.dll'], cwd=d, env=env, text=True, capture_output=True)
        if run.returncode: raise AssertionError(run.stdout + run.stderr)
        cls.lib = c.CDLL(str(d / 'protocol.dll'))
        kernel = c.WinDLL('kernel32'); kernel.FreeLibrary.argtypes = [c.c_void_p]
        cls.addClassCleanup(lambda: kernel.FreeLibrary(cls.lib._handle))
        cls.lib.fw.argtypes = [c.c_void_p, c.c_size_t]
        cls.lib.head.argtypes = [c.c_void_p, c.c_size_t, c.POINTER(c.c_uint)]
        cls.lib.contact.argtypes = [c.c_void_p, c.c_size_t, c.c_uint, c.POINTER(c.c_int32), c.POINTER(c.c_int32)]
        cls.lib.scale.argtypes = [c.c_int32, c.c_int32, c.c_int32]
        cls.lib.addr.argtypes = [c.c_uint32, c.c_void_p]

    @staticmethod
    def seal(b):
        b[-2:] = (sum(b[:-2]) & 0xFFFF).to_bytes(2, 'little')
        return b

    def firmware(self):
        b = bytearray(28); b[0:6] = b'BERLIN'; b[10:14] = b'9895'
        return self.seal(b)

    def header(self, flags=0x80, contacts=1):
        b = bytearray(8); b[0] = flags; b[2] = contacts
        return self.seal(b)

    def block(self, x, y, kind=0):
        b = bytearray(10); b[0] = kind; b[2:4] = x.to_bytes(2, 'little'); b[4:6] = y.to_bytes(2, 'little')
        return self.seal(b)

    def parse_head(self, b):
        n = c.c_uint(99)
        return self.lib.head(bytes(b), len(b), c.byref(n)), n.value

    def test_register_address_is_four_big_endian_bytes(self):
        out = (c.c_uint8 * 4)()
        self.lib.addr(0x010308, out)
        self.assertEqual(list(out), [0x00, 0x01, 0x03, 0x08])

    def test_firmware_block_requires_sum_length_and_9895(self):
        good = self.firmware()
        self.assertTrue(self.lib.fw(bytes(good), len(good)))
        self.assertFalse(self.lib.fw(bytes(good[:-1]), len(good) - 1))
        bad = bytearray(good); bad[5] ^= 1
        self.assertFalse(self.lib.fw(bytes(bad), len(bad)))
        other = bytearray(good); other[10:14] = b'9916'; self.seal(other)
        self.assertFalse(self.lib.fw(bytes(other), len(other)))

    def test_header_classification(self):
        self.assertEqual(self.parse_head(self.header()), (CONTACTS, 1))
        self.assertEqual(self.parse_head(self.header(contacts=0)), (RELEASE, 0))
        self.assertEqual(self.parse_head(self.header(flags=0)), (NONE, 0))
        self.assertEqual(self.parse_head(self.header(flags=0x01)), (IGNORE, 0))
        self.assertEqual(self.parse_head(self.header(contacts=10)), (CONTACTS, 10))

    def test_header_rejects_corruption_and_bad_counts(self):
        for index in (0, 2, 5):
            h = self.header(); h[index] ^= 1 if index == 5 else 0x20
            if index == 0: h[0] = 0x81
            self.assertEqual(self.parse_head(h)[0], INVALID, index)
        self.assertEqual(self.parse_head(self.header(contacts=11))[0], INVALID)
        self.assertEqual(self.parse_head(self.header()[:7])[0], INVALID)

    def test_contact_block_returns_first_finger_and_rejects_bad_input(self):
        x, y = c.c_int32(-1), c.c_int32(-2)
        b = self.block(1059, 2399)
        self.assertTrue(self.lib.contact(bytes(b), len(b), 1, c.byref(x), c.byref(y)))
        self.assertEqual((x.value, y.value), (1059, 2399))
        for label, data, count in (('sum', self.block(5, 6), 1), ('length', self.block(5, 6)[:-1], 1),
                                   ('zero', self.block(5, 6), 0), ('count', self.block(5, 6), 2),
                                   ('stylus', self.block(5, 6, kind=3), 1), ('hover', self.block(5, 6, kind=1), 1)):
            data = bytearray(data)
            if label == 'sum': data[3] ^= 1
            x.value, y.value = -1, -2
            self.assertFalse(self.lib.contact(bytes(data), len(data), count, c.byref(x), c.byref(y)), label)
            self.assertEqual((x.value, y.value), (-1, -2), label)

    def test_scale_maps_raw_to_native_and_clamps(self):
        s = self.lib.scale
        self.assertEqual((s(0, 1060, 568), s(1059, 1060, 568)), (0, 567))
        self.assertEqual((s(0, 2400, 1232), s(2399, 2400, 1232)), (0, 1231))
        self.assertEqual((s(-5, 1060, 568), s(99999, 1060, 568)), (0, 567))
        self.assertEqual(s(530, 1060, 568), 284)
        self.assertEqual(s(10, 0, 568), 0)


if __name__ == '__main__': unittest.main()
