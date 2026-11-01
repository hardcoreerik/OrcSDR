import sys,time,serial,json,pathlib
from serial.tools.list_ports import comports
assert [p.serial_number for p in comports() if p.device=='COM7']==['400'],'tinySA identity mismatch'
p=serial.Serial(port=None,baudrate=115200,timeout=.15,write_timeout=2);p.dtr=False;p.rts=False;p.port='COM7';p.open()
try:
 for cmd in sys.argv[1:]:
  p.write((cmd+'\r').encode());end=time.monotonic()+4;b=b''
  while time.monotonic()<end:
   b+=p.read(4096)
   if b.endswith(b'ch> '):break
  row={'t':time.time(),'command':cmd,'response':b.decode(errors='replace')}
  with pathlib.Path(__file__).with_name('esp-source-commands.jsonl').open('a') as f:f.write(json.dumps(row)+'\n')
  print(cmd,repr(row['response']),flush=True)
  if not b.endswith(b'ch> '):raise RuntimeError('No tinySA prompt')
finally:p.close()
