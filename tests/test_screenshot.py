import base64
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("orcsdr_screenshot",ROOT/"tools/screenshot.py")
shot=importlib.util.module_from_spec(spec);spec.loader.exec_module(shot)

class Port:
    def __init__(self,data): self.data=data
    def reset_input_buffer(self): pass
    @property
    def in_waiting(self): return len(self.data)
    def read(self,n=1): value=self.data[:n];self.data=self.data[n:];return value

class Device:
    def __init__(self,valid=True):
        data=b"\x89PNG\r\n\x1a\nexample"
        digest=hashlib.sha256(data).hexdigest() if valid else "0"*64
        wire=(f"ORC_SNAP_BEGIN width=1 height=1 bytes={len(data)} sha256={digest}\n"
              "unrelated log\n"+f"ORC_SNAP_DATA 0 {base64.b64encode(data).decode()}\nORC_SNAP_END\n").encode()
        self.s=Port(wire);self.commands=[];self.auth=False
    def authenticate(self,key): self.auth=True
    def send(self,command): self.commands.append(command)

class ScreenshotTests(unittest.TestCase):
    def test_generic_command_and_verified_output(self):
        dev=Device()
        with tempfile.TemporaryDirectory() as tmp:
            target=Path(tmp)/"capture.png"
            self.assertEqual(shot.screenshot(dev,"unused",target,tries=1),15)
            self.assertTrue(target.read_bytes().startswith(b"\x89PNG"))
        self.assertTrue(dev.auth);self.assertEqual(dev.commands,["SCREENSHOT"])
    def test_bad_hash_never_writes_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            target=Path(tmp)/"capture.png"
            with self.assertRaises(SystemExit):shot.screenshot(Device(False),"unused",target,tries=1)
            self.assertFalse(target.exists())
    def test_explicit_legacy_command(self):
        dev=Device()
        with tempfile.TemporaryDirectory() as tmp:
            shot.screenshot(dev,"unused",Path(tmp)/"capture.png",tries=1,command="FT8 SNAP")
        self.assertEqual(dev.commands,["FT8 SNAP"])

if __name__=="__main__":unittest.main()
