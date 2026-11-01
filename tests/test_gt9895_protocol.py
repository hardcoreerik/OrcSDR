"""Checksum/length validation for real GT9895 parser, without I2C hardware."""
import ctypes as c
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class ProtocolTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temp=tempfile.TemporaryDirectory();cls.addClassCleanup(temp.cleanup)
        d=Path(temp.name)
        src=ROOT/'ports/p4/components/orcsdr_board_hosyond/src'
        (d/'wrapper.c').write_text('#include "gt9895_protocol.h"\n'
            '__declspec(dllexport) int decode(const uint8_t*p,size_t n,int32_t*x,int32_t*y,bool*down){return gt9895_decode(p,n,x,y,down);}\n'
            '__declspec(dllexport) int address(const uint8_t*p,size_t n,uint32_t*out){return gt9895_report_address(p,n,out);}\n')
        cl=Path(r'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.50.35717/bin/Hostx64/x64/cl.exe')
        ucrt=sorted(Path(r'C:/Program Files (x86)/Windows Kits/10/Include').glob('*/ucrt'))[-1]
        env=dict(os.environ);env['PATH']=str(cl.parent)+os.pathsep+env['PATH']
        run=subprocess.run([str(cl),'/nologo','/LD','/O2','/GS-','/Zl','/I'+str(cl.parents[3]/'include'),
            '/I'+str(ucrt),'/I'+str(src),'wrapper.c','/link','/NOENTRY','/NODEFAULTLIB','/OUT:protocol.dll'],cwd=d,env=env,text=True,capture_output=True)
        if run.returncode:raise AssertionError(run.stdout+run.stderr)
        cls.lib=c.CDLL(str(d/'protocol.dll'))
        kernel=c.WinDLL('kernel32');kernel.FreeLibrary.argtypes=[c.c_void_p]
        cls.addClassCleanup(lambda:kernel.FreeLibrary(cls.lib._handle))
        cls.lib.decode.argtypes=[c.c_void_p,c.c_size_t,c.POINTER(c.c_int32),c.POINTER(c.c_int32),c.POINTER(c.c_bool)]
        cls.lib.address.argtypes=[c.c_void_p,c.c_size_t,c.POINTER(c.c_uint32)]

    @staticmethod
    def checksum(b,start,end):
        b[end-2:end]=(sum(b[start:end-2])&65535).to_bytes(2,'little')

    def packet(self):
        b=bytearray(18);b[0]=128;b[2]=1
        b[10:12]=(1059).to_bytes(2,'little');b[12:14]=(2399).to_bytes(2,'little')
        self.checksum(b,0,8);self.checksum(b,8,18)
        return b

    def decode(self,b):
        x,y,down=c.c_int32(-1),c.c_int32(-2),c.c_bool(False)
        ok=self.lib.decode(bytes(b),len(b),c.byref(x),c.byref(y),c.byref(down))
        return bool(ok),x.value,y.value,down.value

    def test_contact_and_header_valid_release(self):
        b=self.packet();self.assertEqual(self.decode(b),(True,1059,2399,True))
        b[2]=0;b[8:]=b'\xff'*10;self.checksum(b,0,8)
        self.assertEqual(self.decode(b),(True,-1,-2,False))

    def test_reject_corrupt_header_point_short_idle_and_multitouch(self):
        for index in (0,6,10,16):
            b=self.packet();b[index]^=1
            self.assertEqual(self.decode(b),(False,-1,-2,False))
        self.assertFalse(self.decode(self.packet()[:-1])[0])
        b=self.packet();b[0]=0;self.checksum(b,0,8);self.assertFalse(self.decode(b)[0])
        b=self.packet();b[2]=2;self.checksum(b,0,8);self.assertFalse(self.decode(b)[0])

    def test_discovery_rejects_lengths_checksums_and_bad_section_offsets(self):
        b=bytearray(87);b[0:2]=(87).to_bytes(2,'little');b[81:85]=(0x10000).to_bytes(4,'little')
        self.checksum(b,0,87)
        out=c.c_uint32(7)
        self.assertTrue(self.lib.address(bytes(b),len(b),c.byref(out)));self.assertEqual(out.value,0x10000)
        for kind in ('checksum','oversized_section','zero_address','wrapped_address','short'):
            bad=bytearray(b)
            if kind=='checksum':bad[5]^=1
            if kind=='oversized_section':bad[32]=255;self.checksum(bad,0,len(bad))
            if kind=='zero_address':bad[81:85]=b'\0'*4;self.checksum(bad,0,len(bad))
            if kind=='wrapped_address':bad[81:85]=(0xffffffff).to_bytes(4,'little');self.checksum(bad,0,len(bad))
            if kind=='short':bad=bad[:-1]
            out.value=7
            self.assertFalse(self.lib.address(bytes(bad),len(bad),c.byref(out)),kind)
            self.assertEqual(out.value,7)

if __name__=='__main__':unittest.main()
