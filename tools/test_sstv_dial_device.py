"""Paired SSTV bench. Requires existing keys and reciprocal identity diagnostics."""
import sys,time,json,re,argparse
from pathlib import Path
from help_media import Tab5
import tab5_ft8 as t


def fields(line):
 return dict(re.findall(r'(\w+)=("[^"]*"|\S+)',line))


def verify_peers(receiver,dial):
 r,d=fields(receiver),fields(dial)
 for state in (r,d):
  for name in ('self','peer'):
   value=state.get(name,'').lower()
   if not re.fullmatch('[0-9a-f]{32}',value) or value=='0'*32:
    raise RuntimeError('Full nonzero companion identity diagnostics required on both devices')
 if r['self'].lower()!=d['peer'].lower() or r['peer'].lower()!=d['self'].lower():
  raise RuntimeError('Dial is not paired with the specified Tab5')


def check_sound(line,enabled=None):
 state=fields(line)
 if state.get('muted')!='1' or (enabled is not None and state.get('enabled')!=str(int(enabled))):
  raise RuntimeError('Dial sound state did not confirm requested mute/off')
 return state


def saved_image(line,session,previous_file):
 state=fields(line)
 if (state.get('session')==str(session) and state.get('state')=='saved' and
     int(state.get('bytes','0'))>0 and state.get('file') not in (None,'','-',previous_file)):
  return state
 return None


def main(argv=None):
 parser=argparse.ArgumentParser(description='Paired SSTV bench: verifies peers, retunes, injects internal Robot36 and saves to SD. Leaves sound muted/off. No RF transmission.')
 parser.add_argument('--tab5-port',required=True)
 parser.add_argument('--dial-port',required=True)
 parser.add_argument('--key',type=Path,required=True)
 parser.add_argument('--frequency',type=int,required=True)
 parser.add_argument('--out',type=Path,required=True)
 args=parser.parse_args(argv)
 if args.tab5_port.upper()==args.dial_port.upper():parser.error('Ports must differ')
 if not 100000 <= args.frequency <= 2000000000:parser.error('Frequency out of range')
 root=args.out;root.mkdir(parents=True,exist_ok=True)
 log=(root/'device-test.log').open('x',encoding='utf-8')
 p=d=None;rows=[];controls_started=False
 result={'synthetic':True,'checks':{}}
 def note(line):print(line,flush=True);log.write(line+'\n');log.flush()
 def pump(seconds):
  end=time.monotonic()+seconds
  while time.monotonic()<end:
   for who,port in [('TAB5',p.serial),('DIAL',d.s)]:
    line=port.readline().decode('utf-8','replace').strip()
    if line.startswith(('SSTV_','ORCDIAL_','RTL_ORCDIAL','TX seq=','ACTION_REJECTED','ACK_TIMEOUT')):
     rows.append((who,line));note(who+' '+line)
 def send(who,command):
  if who=='TAB5' and 'STATUS' not in command:p.authenticate(create_key=False)
  note(who+' > '+command);(p.serial if who=='TAB5' else d.s).write((command+'\n').encode())
 def query(who,command,prefix,timeout=6):
  pump(.05) # discard earlier replies before attributing a response to this command
  start=len(rows);send(who,command);end=time.monotonic()+timeout;next_poll=time.monotonic()+1
  while time.monotonic()<end:
   pump(.08)
   for device,line in reversed(rows[start:]):
    if device==who and line.startswith(prefix):return line
   if 'STATUS' in command and time.monotonic()>next_poll:send(who,command);next_poll=time.monotonic()+1
  raise RuntimeError('No '+prefix+' response; identity diagnostics required for peer checks')
 def archive(session):
  end=time.monotonic()+10
  while time.monotonic()<end:
   start=len(rows);query('TAB5','SSTV STATUS','SSTV_SELFTEST ')
   current=[line for who,line in rows[start:] if who=='TAB5']
   states=[fields(line) for line in current if line.startswith('SSTV_ARCHIVE ')]
   if states and states[-1].get('pending')=='0':
    return [line for line in current if line.startswith('SSTV_RESULT ') and fields(line).get('session')==str(session)]
   pump(.2)
  raise RuntimeError('Archive did not drain before save verification')
 try:
  if not args.key.is_file():raise FileNotFoundError('An existing serial authorization key file is required')
  p=Tab5(args.tab5_port,args.key);d=t.Tab5(args.dial_port)
  p.serial.timeout=.02;d.s.timeout=.02
  status=fields(query('DIAL','ORCDIAL_STATUS','ORCDIAL_STATUS link=LINKED pairing=',20))
  receiver=fields(query('TAB5','RTL_ORCDIAL_STATUS','RTL_ORCDIAL_STATUS '))
  if status.get('pending')!='0' or receiver.get('trust')!='1' or receiver.get('connection')!='Connected':
   raise RuntimeError('Both companion endpoints must be connected and idle')
  verify_peers(query('TAB5','RTL_ORCDIAL_STATUS','RTL_ORCDIAL_IDENTITY '),
               query('DIAL','ORCDIAL_STATUS','ORCDIAL_IDENTITY '))
  result['checks']['paired_identity']='PASS'
  p.authenticate(create_key=False);note('AUTH verified; key and proofs omitted')
  controls_started=True
  query('TAB5','SSTV OPEN','SSTV_OK dashboard=1',15);pump(2)
  query('TAB5','SSTV DETECT AUTO','SSTV_OK detection=0',10)
  query('TAB5',f'SSTV TUNE {args.frequency}','SSTV_OK',15);pump(2)
  query('DIAL','ORCDIAL_SOUND UNMUTE','ORCDIAL_SOUND ')
  query('DIAL','ORCDIAL_SOUND ON','ORCDIAL_SOUND ')
  before=fields(query('DIAL','ORCDIAL_SSTV STATUS','ORCDIAL_SSTV '))
  if before['tune']=='0':send('DIAL','ORCDIAL_SSTV PAGE');pump(.3)
  query('TAB5','SSTV SELFTEST ROBOT36 SPEED 2','SSTV_OK selftest=',8)
  start=time.monotonic();received_session=None;completed=False
  while time.monotonic()-start<45:
   snapshot=fields(query('DIAL','ORCDIAL_SSTV STATUS','ORCDIAL_SSTV '))
   if snapshot.get('valid')!='1':raise RuntimeError('SSTV status is not live')
   query('TAB5','SSTV STATUS','SSTV_STATUS ')
   if int(snapshot['row'])>0 and int(snapshot['state']) in (3,4,5) and received_session is None:
    received_session=int(snapshot['session'])
    freq=fields(query('DIAL','ORCDIAL_STATUS','ORCDIAL_STATUS link=LINKED pairing='))['freq']
    send('DIAL','ORCDIAL_ROTATE 1');pump(.4)
    if fields(query('DIAL','ORCDIAL_STATUS','ORCDIAL_STATUS link=LINKED pairing='))['freq']!=freq:
     raise RuntimeError('Retuning was not blocked during reception')
    result['checks']['retune_blocked_during_rx']='PASS'
   if int(snapshot['complete'])>int(before['complete']):completed=True;break
   pump(1)
  if received_session is None or not completed:raise RuntimeError('New Robot36 progress/completion not received on Dial')
  result['checks']['synthetic_progress_complete']='PASS'
  query('TAB5','SSTV SELFTEST STOP','SSTV_OK selftest=stopped')
  baseline=archive(received_session)
  previous_file=fields(baseline[0]).get('file') if baseline else None
  query('DIAL','ORCDIAL_SSTV SAVE','ORCDIAL_SSTV_CONFIRM_REQUIRED')
  sent=fields(query('DIAL','ORCDIAL_SSTV SAVE','TX seq='))
  if sent.get('type')!='16' or sent.get('value')!='1':raise RuntimeError('Confirmed save did not transmit its expected action')
  end=time.monotonic()+4
  while time.monotonic()<end:
   status=fields(query('DIAL','ORCDIAL_STATUS','ORCDIAL_STATUS link=LINKED pairing='))
   if status.get('ack')==sent['seq'] and status.get('pending')=='0':break
  else:raise RuntimeError('Save action acknowledgement did not match its command sequence')
  results=archive(received_session)
  image=next((saved_image(line,received_session,previous_file) for line in results if saved_image(line,received_session,previous_file)),None)
  if image is None:raise RuntimeError('No new saved filename for this test image after confirmed save')
  result.update(saved_image=image,save_sequence=sent['seq'])
  result['checks']['confirmed_save']='PASS'
  check_sound(query('DIAL','ORCDIAL_SOUND MUTE','ORCDIAL_SOUND '))
  result['final_sound']=check_sound(query('DIAL','ORCDIAL_SOUND OFF','ORCDIAL_SOUND '),enabled=False)
  if snapshot['tune']=='1':send('DIAL','ORCDIAL_SSTV PAGE');pump(.3)
  result['overall']='PASS'
 except Exception as error:
  result.update(overall='FAIL',error=str(error));note('ERROR '+str(error))
 finally:
  if controls_started:
   for who,command,prefix in [('TAB5','SSTV SELFTEST STOP','SSTV_OK selftest=stopped'),
                              ('DIAL','ORCDIAL_SOUND MUTE','ORCDIAL_SOUND '),('DIAL','ORCDIAL_SOUND OFF','ORCDIAL_SOUND ')]:
    try:
     reply=query(who,command,prefix)
     if who=='DIAL':check_sound(reply,enabled=False if command.endswith('OFF') else None)
    except Exception as error:
     result['overall']='FAIL';result.setdefault('cleanup_errors',[]).append(who+' '+command+': '+str(error))
  for close in ([p.close] if p is not None else [])+([d.s.close] if d is not None else []):
   try:close()
   except Exception as error:
    result['overall']='FAIL';result.setdefault('cleanup_errors',[]).append('serial close: '+str(error))
  log.close();(root/'device-result.json').write_text(json.dumps(result,indent=2));print(json.dumps(result),flush=True)
 return 0 if result['overall']=='PASS' else 1


if __name__=='__main__':sys.exit(main())
