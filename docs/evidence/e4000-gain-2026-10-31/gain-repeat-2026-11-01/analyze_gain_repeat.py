import json,hashlib
from pathlib import Path
import numpy as np
base=Path('apps/orcsdr-tab5/build-offset/gain-candidate/gain-repeat-20261101')
e=json.loads((base/'events.json').read_text());rows=[]
for r in e:
 if r['kind']!='gain_result':continue
 p=base/(r['name']+'.cu8');raw=p.read_bytes();assert hashlib.sha256(raw).hexdigest()==r['sha256']
 a=np.frombuffer(raw,np.uint8);z=(a[::2].astype(float)-127.5+1j*(a[1::2].astype(float)-127.5))/128
 n=65536;w=np.hanning(n);x=z[:len(z)//n*n].reshape(-1,n);power=abs(np.fft.fft(x*w,axis=1)/w.sum())**2
 k=round(100000/2400000*n);v=10*np.log10(power[:,k-4:k+5].sum(axis=1))
 rows.append(dict(name=r['name'],gain_db=r['gain_tenth_db']/10,tone_integrated_dbfs=float(10*np.log10(np.mean(10**(v/10)))),block_min_dbfs=float(v.min()),block_max_dbfs=float(v.max()),clip_fraction=r['clip_fraction'],sha256=r['sha256']))
print(json.dumps(rows,indent=2))
(base/'integrated-results.json').write_text(json.dumps(rows,indent=2)+'\n')
