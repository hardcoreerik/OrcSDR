
import sys,pathlib,time,json,re,hashlib,subprocess
import numpy as np
sys.path.insert(0,str(pathlib.Path('tools').resolve()))
from tab5_ft8 import Tab5,find_key
root=pathlib.Path('apps/orcsdr-tab5/build-offset/gain-candidate/gain-repeat-20261101');root.mkdir(exist_ok=False)
events=[];d=None
source='C:/Tools/esp-rtl-sdr-lab/captures/e4000-xtr-20261031/tinysa_command.py'
def event(kind,**kw):
 events.append(dict(t=time.time(),kind=kind,**kw));(root/'events.json').write_text(json.dumps(events,indent=2))
def src(*commands):
 p=subprocess.run([sys.executable,source,*commands],capture_output=True,text=True);event('source',commands=commands,response=p.stdout);assert p.returncode==0,p.stderr

def read(prefix,timeout=8):
 lines=d.read_until((prefix,'RTL_IQ_ERROR ','RTL_IQ_GET_ERROR ','AUTH_REQUIRED'),timeout,echo=False);m=[x for x in lines if x.startswith(prefix)];assert m,'Missing '+prefix;return m[-1]
def query(command,prefix):
 d.send(command);line=read(prefix);event('serial',command=command,response=line);return line

def capture(name):
 status=query('RTL_IQ_DIAG_STATUS','RTL_IQ_DIAG_STATUS ')
 if 'ready=1' in status:query('RTL_IQ_RETRIEVE_END','RTL_IQ_RETRIEVE_')
 line=query('RTL_IQ_DIAG_START '+name,'RTL_IQ_DIAG_START ');n=int(re.search(r' bytes=(\d+)',line)[1]);read('RTL_IQ_DONE storage=psram source=diagnostic ',15)
 query('RTL_STOP','RTL_STOP_RESULT ')
 line=query('RTL_IQ_GET_BEGIN','RTL_IQ_GET_READY ');assert int(re.search(r'bytes=(\d+)',line)[1])==n
 path=root/(name+'.cu8');buf=bytearray()
 with path.open('xb') as f:
  while len(buf)<n:
   d.send('RTL_IQ_GET_CHUNK');line=read('RTL_IQ_GET_DATA ');count=int(re.search(r'bytes=(\d+)',line)[1]);assert 0<count<=2048 and count<=n-len(buf)
   payload=bytearray();end=time.monotonic()+8
   while len(payload)<count and time.monotonic()<end:payload.extend(d.s.read(count-len(payload)))
   assert len(payload)==count;f.write(payload);buf.extend(payload)
 line=read('RTL_IQ_GET_DONE ');sha=hashlib.sha256(buf).hexdigest();assert 'sha256='+sha in line
 query('RTL_IQ_RETRIEVE_END','RTL_IQ_RETRIEVE_')
 a=np.frombuffer(buf,np.uint8);z=(a[0::2].astype(float)-127.5+1j*(a[1::2].astype(float)-127.5))/128
 N=65536;count=min(16,len(z)//N);w=np.hanning(N);power=np.mean(abs(np.fft.fft(z[:N*count].reshape(-1,N)*w,axis=1)/w.sum())**2,axis=0);freq=np.fft.fftfreq(N,1/2400000);sel=abs(freq-100000)<3000;k=np.flatnonzero(sel)[np.argmax(power[sel])]
 row=dict(name=name,bytes=n,sha256=sha,tone_hz=float(freq[k]),tone_dbfs=float(10*np.log10(power[k])),rms_dbfs=float(10*np.log10(np.mean(abs(z)**2))),clip_fraction=float(np.mean((a==0)|(a==255))),min=int(a.min()),max=int(a.max()),mean_i=float(a[0::2].mean()),mean_q=float(a[1::2].mean()));event('capture',**row);print(json.dumps(row),flush=True)
 return row

def start(gain=-10):
 query('RTL_TUNE FM 100000000','RTL_TUNE_OK ')
 end=time.monotonic()+12
 while time.monotonic()<end:
  line=query('RTL_DRIVER STATUS','RTL_DRIVER_STATUS ')
  if 'state=STREAMING' in line and 'frequency_hz=100000000 ' in line:break
  time.sleep(.3)
 else:raise RuntimeError('Receiver did not settle')
 line=query('RTL_DRIVER GAIN '+str(gain),'RTL_DRIVER_RESULT ');assert 'accepted=1' in line
 end=time.monotonic()+6
 while time.monotonic()<end:
  line=query('RTL_DRIVER STATUS','RTL_DRIVER_STATUS ')
  if 'state=STREAMING' in line and 'mode=MANUAL ' in line and ('gain_tenth_db='+str(gain)+' ') in line:
   time.sleep(1);break
  time.sleep(.2)
 else:raise RuntimeError('Applied manual gain did not settle')
try:
 src('output off','pause','mode low output','pause','level -40','modulation off','sweep cw 100100000','resume','output off')
 d=Tab5('COM39');d.send('');query('RTL_HEALTH','RTL_HEALTH_STATUS ');d.authenticate(find_key('F:/Ai/OrcSDR/.orclink/ui-doc.key'))
 start();off=capture('source-off-minus1')
 src('output on')
 for index,gain in enumerate([140,165,190,165,140,290,340,290]):
  d.authenticate(find_key('F:/Ai/OrcSDR/.orclink/ui-doc.key'));start(gain)
  query('RTL_DRIVER STATUS','RTL_DRIVER_STATUS ')
  row=capture('repeat-'+str(index)+'-gain-'+str(gain));row['gain_tenth_db']=gain;event('gain_result',**row)
  assert row['clip_fraction']<.0001, 'RF clipping: stop matrix'
  assert abs(row['tone_hz']-100000)<2000 and row['tone_dbfs']-off['tone_dbfs']>15, 'Source tone not verified'
 event('run_complete',passed=True)
 src('output off');d.authenticate(find_key('F:/Ai/OrcSDR/.orclink/ui-doc.key'));start(-10)

finally:
 try:src('output off','pause')
 finally:
  if d:d.s.close()
