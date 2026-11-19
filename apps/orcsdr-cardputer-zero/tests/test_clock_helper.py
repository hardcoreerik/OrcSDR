import importlib.machinery,importlib.util,unittest,subprocess,contextlib,io
from unittest.mock import patch
from pathlib import Path
loader=importlib.machinery.SourceFileLoader("clock",str(Path(__file__).parents[1]/"tools/orcsdr-clock"))
spec=importlib.util.spec_from_loader(loader.name,loader);clock=importlib.util.module_from_spec(spec);loader.exec_module(clock)
class ClockTests(unittest.TestCase):
 def test_valid(self):
  self.assertEqual(clock.parse_utc("20280229235959").day,29)
 def test_reject(self):
  for value in ["20260229000000","20260101246000","20260101000000;id","２０２６０１０１００００００","20230101000000","21000101000000",""]:
   with self.subTest(value=value),self.assertRaises(ValueError):clock.parse_utc(value)
 def test_manual_status_without_daemon(self):
  def fake(*args):
   if "show-timesync" in args:raise subprocess.CalledProcessError(1,args)
   return "NTP=no\nNTPSynchronized=no\n"
  out=io.StringIO()
  with patch.object(clock,"run",fake),contextlib.redirect_stdout(out):
   self.assertEqual(clock.main(["--status"]),0)
  self.assertIn("ServerName=",out.getvalue())
 def test_no_command_on_invalid(self):
  clock.run=lambda *a:self.fail("Invalid date executed a command")
  with self.assertRaises(ValueError):clock.main(["--set","20260230000000"])
if __name__=="__main__":unittest.main()
