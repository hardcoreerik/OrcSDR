import sys,time,json,re,argparse
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from help_media import Tab5
import tab5_ft8 as t
parser=argparse.ArgumentParser(description="Paired SSTV bench: injects internal Robot36, retunes, saves to SD; leaves alerts off and muted. No RF transmission.")
parser.add_argument('--tab5-port',required=True)
parser.add_argument('--dial-port',required=True)
parser.add_argument('--key',type=Path,required=True)
parser.add_argument('--frequency',type=int,required=True)
parser.add_argument('--out',type=Path,required=True)
args=parser.parse_args()
if args.tab5_port.upper()==args.dial_port.upper():parser.error('Ports must differ')
if not 100000 <= args.frequency <= 2000000000:parser.error('Frequency out of range')
root=args.out;root.mkdir(parents=True,exist_ok=True)
log=(root/'device-test.log').open('x',encoding='utf-8')
def note(s): print(s,flush=True);log.write(s+'\n');log.flush()
p=d=None
rows=[]
def pump(seconds):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  for who,port in [('TAB5',p.serial),('DIAL',d.s)]:
   line=port.readline().decode('utf-8','replace').strip()
   if line.startswith(('SSTV_','ORCDIAL_SOUND','ORCDIAL_SSTV','ORCDIAL_SECURITY','ORCDIAL_STATUS','RTL_ORCDIAL','TX seq=','ACTION_REJECTED','ACK_TIMEOUT')):
    rows.append((who,line));note(who+' '+line)
def send(who,cmd):
 if who=='TAB5' and 'STATUS' not in cmd:p.authenticate()
 note(who+' > '+cmd);(p.serial if who=='TAB5' else d.s).write((cmd+'\n').encode())
def query(who,cmd,prefix,timeout=6):
 start=len(rows);send(who,cmd);end=time.monotonic()+timeout;next_poll=time.monotonic()+1
 while time.monotonic()<end:
  pump(.08)
  if 'STATUS' in cmd and time.monotonic()>next_poll:send(who,cmd);next_poll=time.monotonic()+1
  for w,s in reversed(rows[start:]):
   if w==who and s.startswith(prefix):return s
 raise RuntimeError('No '+prefix)
def fields(s):return dict(re.findall(r'(\w+)=(\S+)',s))
result={'synthetic':True,'checks':{}}
try:
 p=Tab5(args.tab5_port,args.key)
 d=t.Tab5(args.dial_port)
 p.serial.timeout=.02;d.s.timeout=.02
 p.authenticate();note('AUTH verified; key and proofs omitted')
 status=query('DIAL','ORCDIAL_STATUS','ORCDIAL_STATUS link=LINKED pairing=',20)
 assert fields(status)['pending']=='0';result['checks']['paired']='PASS'
 query('TAB5','SSTV OPEN','SSTV_OK dashboard=1',15);pump(2)
 query('TAB5','SSTV STATUS','SSTV_STATUS ');pump(.3)
 query('TAB5','SSTV DETECT AUTO','SSTV_OK detection=0',10)
 query('TAB5',f'SSTV TUNE {args.frequency}','SSTV_OK',15);pump(2)
 query('DIAL','ORCDIAL_SOUND UNMUTE','ORCDIAL_SOUND ')
 query('DIAL','ORCDIAL_SOUND ON','ORCDIAL_SOUND ')
 before=fields(query('DIAL','ORCDIAL_SSTV STATUS','ORCDIAL_SSTV '))
 if before['tune']=='0':send('DIAL','ORCDIAL_SSTV PAGE');pump(.3)
 query('TAB5','SSTV SELFTEST ROBOT36 SPEED 2','SSTV_OK selftest=',8)
 start=time.monotonic();saw_receive=False;completed=False;first=None
 while time.monotonic()-start<45:
  snapshot=fields(query('DIAL','ORCDIAL_SSTV STATUS','ORCDIAL_SSTV '))
  query('DIAL','ORCDIAL_SOUND STATUS','ORCDIAL_SOUND ')
  query('TAB5','SSTV STATUS','SSTV_STATUS ')
  if int(snapshot['row'])>0 and int(snapshot['state']) in (3,4,5):
   saw_receive=True
   if first is None:
    first=int(snapshot['row']);freq=fields(query('DIAL','ORCDIAL_STATUS','ORCDIAL_STATUS link=LINKED pairing='))['freq']
    send('DIAL','ORCDIAL_ROTATE 1');pump(.4)
    verify=fields(query('DIAL','ORCDIAL_STATUS','ORCDIAL_STATUS link=LINKED pairing='));assert verify['freq']==freq
    result['checks']['retune_blocked_during_rx']='PASS'
  if int(snapshot['complete'])>int(before['complete']):completed=True;break
  pump(1)
 assert saw_receive and completed,'Synthetic Robot36 progress/completion not received on Dial'
 result['checks']['synthetic_progress_complete']='PASS'
 saved_before=int(snapshot['saved'])
 send('DIAL','ORCDIAL_SSTV SAVE');pump(.2);send('DIAL','ORCDIAL_SSTV SAVE');pump(3)
 snapshot=fields(query('DIAL','ORCDIAL_SSTV STATUS','ORCDIAL_SSTV '));result['saved_snapshot']=snapshot
 assert int(snapshot['saved'])>saved_before,'Confirmed save did not advance archive counter'
 result['checks']['confirmed_save']='PASS'
 query('DIAL','ORCDIAL_SOUND MUTE','ORCDIAL_SOUND ')
 query('DIAL','ORCDIAL_SOUND OFF','ORCDIAL_SOUND ')
 if snapshot['tune']=='1':send('DIAL','ORCDIAL_SSTV PAGE');pump(.3)
 query('TAB5','SSTV SELFTEST STOP','SSTV_OK selftest=stopped')
 result['overall']='PASS'
except Exception as e:
 result.update(overall='FAIL',error=str(e));note('ERROR '+str(e))
finally:
 if p is not None and d is not None:
  for who,command in [('TAB5','SSTV SELFTEST STOP'),('DIAL','ORCDIAL_SOUND MUTE'),('DIAL','ORCDIAL_SOUND OFF')]:
   try:send(who,command)
   except Exception as error:
    result['overall']='FAIL';result.setdefault('cleanup_errors',[]).append(who+' '+command+': '+str(error))
  try:pump(.3)
  except Exception as error:
   result['overall']='FAIL';result.setdefault('cleanup_errors',[]).append('serial drain: '+str(error))
 for close in ([p.close] if p is not None else [])+([d.s.close] if d is not None else []):
  try:close()
  except Exception as error:result.update(overall='FAIL',cleanup_error=str(error))
 log.close();(root/'device-result.json').write_text(json.dumps(result,indent=2));print(json.dumps(result),flush=True)

sys.exit(0 if result['overall']=='PASS' else 1)
