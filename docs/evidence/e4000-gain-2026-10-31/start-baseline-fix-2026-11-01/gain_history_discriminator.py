import sys,time,json,re,subprocess
from pathlib import Path
sys.path.insert(0,str(Path('tools').resolve()))
from tab5_ft8 import Tab5,find_key
from tab5_exact_response import read_response
root=Path('apps/orcsdr-tab5/build-offset/readback-candidate/gain-history-discriminator');root.mkdir(exist_ok=False)
events=[];d=None
source='C:/Tools/esp-rtl-sdr-lab/captures/e4000-xtr-20261031/tinysa_command.py'
def log(kind,**kw):
 events.append(dict(t=time.time(),kind=kind,**kw));(root/'events.json').write_text(json.dumps(events,indent=2))
def src(*cmds):
 r=subprocess.run([sys.executable,source,*cmds],capture_output=True,text=True);log('source',commands=cmds,response=r.stdout);assert r.returncode==0,r.stderr
def q(cmd,prefix,public=False):
 d.send(cmd);line=read_response(d.s,prefix,12,public);log('serial',command=cmd,response=line);return line
def fields(line):return dict(re.findall(r'(\w+)=(\S+)',line))
def gain(g):
 assert 'accepted=1' in q('RTL_DRIVER GAIN '+str(g),'RTL_DRIVER_RESULT ')
 end=time.monotonic()+8
 while time.monotonic()<end:
  f=fields(q('RTL_DRIVER STATUS','RTL_DRIVER_STATUS '))
  if f['state']=='STREAMING' and f['mode']=='MANUAL' and int(f['gain_tenth_db'])==g:return
  time.sleep(.2)
 raise RuntimeError('Gain did not settle')
def start():
 q('RTL_TUNE FM 100000000','RTL_TUNE_OK ')
 end=time.monotonic()+12
 while time.monotonic()<end:
  if 'state=STREAMING' in q('RTL_DRIVER STATUS','RTL_DRIVER_STATUS '):return
  time.sleep(.3)
 raise RuntimeError('No stream')
def sample(label):
 time.sleep(4)
 vals=[]
 for i in range(10):
  f=fields(q('RTL_SIGNAL','RTL_SIGNAL_STATUS '));vals.append(int(f['signal_dbfs_tenths'])/10);time.sleep(.3)
 status=q('RTL_DRIVER STATUS','RTL_DRIVER_STATUS ')
 assert 'state=STREAMING' in status
 log('measurement',label=label,levels_dbfs=vals,mean_dbfs=sum(vals)/len(vals))
 print(label,round(sum(vals)/len(vals),3),flush=True)
try:
 src('output off','pause','mode low output','pause','level -40','modulation off','sweep cw 100100000','resume','output off')
 d=Tab5('COM39');d.send('');time.sleep(.3);q('RTL_HEALTH','RTL_HEALTH_STATUS ',True);d.authenticate(find_key('F:/Ai/OrcSDR/.orclink/ui-doc.key'))
 assert 'profile=5 ' in q('RTL_DRIVER STATUS','RTL_DRIVER_STATUS ')
 start();gain(-10);src('output on');gain(140);sample('live14_initial')
 q('RTL_STOP','RTL_STOP_RESULT ');start();sample('restart14_lower')
 gain(-10);gain(140);sample('live_low_then14')
 gain(-10)
 assert 'accepted=1' in q('RTL_DRIVER TUNE 100000000','RTL_DRIVER_RESULT ')
 time.sleep(1);gain(140);sample('retune_low_then14')
 gain(-10);q('RTL_STOP','RTL_STOP_RESULT ');start();gain(140);sample('restart_low_then14')
 log('complete',passed=True)
finally:
 try:src('output off','pause')
 finally:
  if d:d.s.close()
