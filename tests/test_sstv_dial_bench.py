import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

TOOL=Path(__file__).resolve().parents[1]/'tools'/'test_sstv_dial_device.py'
spec=importlib.util.spec_from_file_location('sstv_bench',TOOL)
bench=importlib.util.module_from_spec(spec)
with patch.dict(sys.modules,{'help_media':SimpleNamespace(Tab5=Mock()),'tab5_ft8':SimpleNamespace(Tab5=Mock())}):
 spec.loader.exec_module(bench)


class Fixture:
 def __init__(self,**faults):
  self.faults=faults;self.commands=[];self.queues={'TAB5':[],'DIAL':[]}
  self.enabled=0;self.muted=1;self.started=False;self.polls=0;self.confirm=False;self.ack=0
  self.file='auto7.bmp';self.stop_count=0;self.clock=0
  self.ports={who:SimpleNamespace(write=lambda data,w=who:self.write(w,data),
                                 readline=lambda w=who:self.read(w),close=Mock()) for who in self.queues}
  self.tablet=SimpleNamespace(serial=self.ports['TAB5'],close=self.ports['TAB5'].close,authenticate=Mock())
  self.dial=SimpleNamespace(s=self.ports['DIAL'])
 def read(self,who):return self.queues[who].pop(0) if self.queues[who] else b''
 def now(self):self.clock+=.01;return self.clock
 def write(self,who,data):
  cmd=data.decode().strip();self.commands.append((who,cmd));reply=[]
  identity='self='+('a'*32)+' peer='+('b'*32)
  if who=='TAB5':
   if cmd=='RTL_ORCDIAL_STATUS':reply=['RTL_ORCDIAL_IDENTITY '+identity,'RTL_ORCDIAL_STATUS bridge=1 trust=1 connection=Connected']
   elif cmd=='SSTV OPEN':reply=['SSTV_OK dashboard=1']
   elif cmd=='SSTV DETECT AUTO':reply=['SSTV_OK detection=0']
   elif cmd.startswith('SSTV TUNE'):reply=['SSTV_OK tuned_hz=14230000']
   elif cmd=='SSTV STATUS':
    session=8 if self.faults.get('wrong_save_session') and self.file!='auto7.bmp' else 7
    reply=['SSTV_STATUS active=1','SSTV_ARCHIVE pending=0 saved=42',
           f'SSTV_RESULT session={session} state=saved bytes=230454 file={self.file}',
           f'SSTV_SELFTEST running={int(self.started)}']
   elif cmd=='SSTV SELFTEST STOP':
    self.stop_count+=1
    if self.faults.get('stop_raises'):raise OSError('STOP failed')
    if self.faults.get('drop_cleanup_stop') and self.stop_count>1:return
    self.started=False;reply=['SSTV_OK selftest=stopped']
   elif cmd.startswith('SSTV SELFTEST ROBOT36'):self.started=True;reply=['SSTV_OK selftest=ROBOT36']
  else:
   if cmd=='ORCDIAL_STATUS':
    peer='c'*32 if self.faults.get('wrong_peer') else 'a'*32
    reply=[f'ORCDIAL_IDENTITY self={"b"*32} peer={peer}',f'ORCDIAL_STATUS link=LINKED pairing=0 pending=0 ack={self.ack} freq=14230000']
   elif cmd.startswith('ORCDIAL_SOUND'):
    if cmd.endswith('UNMUTE'):self.muted=0
    elif cmd.endswith('MUTE') and not self.faults.get('ignore_mute'):self.muted=1
    elif cmd.endswith('ON'):
     self.enabled=1
     if self.faults.get('fail_after_on'):raise OSError('failed after ON')
    elif cmd.endswith('OFF') and not self.faults.get('ignore_off'):self.enabled=0
    reply=[f'ORCDIAL_SOUND enabled={self.enabled} muted={self.muted}']
   elif cmd=='ORCDIAL_SSTV STATUS':
    if self.started:self.polls+=1
    receiving=self.started and self.polls==1;done=self.polls>=2
    reply=[f'ORCDIAL_SSTV valid=1 tune=1 state={4 if receiving else 7} session={7 if receiving else 0} row={20 if receiving else 0} complete={int(done)}']
   elif cmd=='ORCDIAL_SSTV SAVE':
    if not self.confirm:self.confirm=True;reply=['ORCDIAL_SSTV_CONFIRM_REQUIRED']
    else:
     self.ack=41 if self.faults.get('wrong_ack') else 42
     if not self.faults.get('unrelated_save'):self.file='manual7.bmp'
     reply=['TX seq=42 type=16 value=1']
  if not reply and cmd not in ('ORCDIAL_ROTATE 1','ORCDIAL_SSTV PAGE'):
   raise AssertionError('Unexpected command '+who+' '+cmd)
  self.queues[who].extend((line+'\n').encode() for line in reply)


class SstvBenchTests(unittest.TestCase):
 def run_fixture(self,fixture,missing_key=False):
  with tempfile.TemporaryDirectory() as temp:
   root=Path(temp);key=root/'existing.key';output=root/'result'
   if not missing_key:key.write_text('ab'*32)
   args=['--tab5-port','COM17','--dial-port','COM14','--key',str(key),'--frequency','14230000','--out',str(output)]
   with patch.object(bench,'Tab5',return_value=fixture.tablet) as tablet_open, \
        patch.object(bench.t,'Tab5',return_value=fixture.dial), \
        patch.object(bench.time,'monotonic',side_effect=fixture.now),contextlib.redirect_stdout(io.StringIO()):
    code=bench.main(args)
   return code,json.loads((output/'device-result.json').read_text()),tablet_open
 def test_success_requires_matching_identity_ack_image_and_final_sound(self):
  f=Fixture();code,r,_=self.run_fixture(f)
  self.assertEqual(code,0);self.assertEqual(r['saved_image']['session'],'7')
  self.assertEqual(r['saved_image']['file'],'manual7.bmp');self.assertEqual(r['save_sequence'],'42')
  self.assertEqual((f.enabled,f.muted),(0,1));self.assertFalse(f.started)
  self.assertTrue(all(call.kwargs=={'create_key':False} for call in f.tablet.authenticate.call_args_list))
 def test_missing_key_never_opens_devices(self):
  f=Fixture();code,r,opened=self.run_fixture(f,missing_key=True)
  self.assertEqual(code,1);opened.assert_not_called();self.assertEqual(f.commands,[])
 def test_wrong_peer_never_sends_controls_or_authenticates(self):
  f=Fixture(wrong_peer=True);code,r,_=self.run_fixture(f)
  self.assertEqual(code,1);f.tablet.authenticate.assert_not_called()
  self.assertTrue(all(cmd in ('RTL_ORCDIAL_STATUS','ORCDIAL_STATUS') for _,cmd in f.commands))
 def test_reject_zero_or_missing_identity(self):
  for value in ('','0'*32):
   with self.assertRaises(RuntimeError):bench.verify_peers('self='+value+' peer='+'b'*32,'self='+'b'*32+' peer='+'a'*32)
 def test_unrelated_archive_counter_cannot_satisfy_save(self):
  code,r,_=self.run_fixture(Fixture(unrelated_save=True));self.assertEqual(code,1)
  self.assertNotIn('confirmed_save',r['checks'])
 def test_wrong_image_session_cannot_satisfy_save(self):
  code,r,_=self.run_fixture(Fixture(wrong_save_session=True));self.assertEqual(code,1)
  self.assertNotIn('confirmed_save',r['checks'])
 def test_save_requires_exact_command_acknowledgement(self):
  code,r,_=self.run_fixture(Fixture(wrong_ack=True));self.assertEqual(code,1)
  self.assertNotIn('confirmed_save',r['checks'])
 def test_unchanged_sound_response_is_a_failure(self):
  for fault in ('ignore_mute','ignore_off'):
   with self.subTest(fault=fault):
    code,r,_=self.run_fixture(Fixture(**{fault:True}));self.assertEqual(code,1)
    self.assertTrue(r['cleanup_errors'])
 def test_cleanup_stop_timeout_invalidates_success(self):
  code,r,_=self.run_fixture(Fixture(drop_cleanup_stop=True));self.assertEqual(code,1)
  self.assertTrue(any('SSTV SELFTEST STOP' in e for e in r['cleanup_errors']))
 def test_tablet_stop_failure_still_mutes_and_disables_dial(self):
  f=Fixture(fail_after_on=True,stop_raises=True);code,r,_=self.run_fixture(f)
  self.assertEqual(code,1);self.assertEqual((f.enabled,f.muted),(0,1))
  self.assertTrue(any('SSTV SELFTEST STOP' in e for e in r['cleanup_errors']))


if __name__=='__main__':unittest.main()
