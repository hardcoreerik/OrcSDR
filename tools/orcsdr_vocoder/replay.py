#!/usr/bin/env python3
"""Experimental BS single-slot offline voice proof. No live acceptance claim."""
import sys, subprocess, wave, hashlib
from pathlib import Path
import numpy as np
from scipy.signal import resample_poly, correlate, find_peaks, firwin
P={'BS voice':0x755FD7DF75F7,'BS data':0xDFF57D75DF5D,'MS voice':0x7F7D5DD57DFD,'MS data':0xD5D7F77FD757,'TS1 voice':0x5D577F7757FF,'TS2 voice':0x7DFFD5F55D5F,'TS1 data':0xF7FDD5DDFD55,'TS2 data':0xD7557F5FF7F5}
par=[0x04f,0x11e,0x1b7,0x1e2,0x1c9,0x0e5,0x073]
words=[]
for v in range(128):
 w=v<<9
 for k in range(7):
  if v&(1<<(6-k)):w^=par[k]
 words.append(w)
def bitsword(b):
 v=0
 for z in b:v=(v<<1)|int(z)
 return v
def qr(b):
 w=bitsword(b); ds=[(w^c).bit_count() for c in words];m=min(ds)
 if m>2 or ds.count(m)!=1:return None
 v=ds.index(m);return (v>>3,(v>>2)&1,v&3,m)
def getburst(d,t,off,amp):
 pos=t+np.arange(-54,78)*10
 if pos[0]<2 or pos[-1]+2>=len(d):return None
 v=np.mean(d[pos[:,None]+np.arange(-2,3)],axis=1)-off
 levels=np.array([amp/3,amp,-amp/3,-amp]);dib=np.argmin(abs(v[:,None]-levels),axis=1)
 return np.column_stack([dib>>1,dib&1]).astype('uint8').ravel()
def lc(frames):
 if len(frames)<5:return None
 m=np.concatenate([b[116:148] for b in frames[1:5]]).reshape(16,8).T.copy()
 parity=[19,26,31,28,14,21,11,22,25,13,7,16,8,4,2,1]
 for row in m[:7]:
  synd=0
  for bit,h in zip(row,parity):
   if bit:synd^=h
  if synd:
   if synd not in parity:return None
   row[parity.index(synd)]^=1
 if np.any(np.sum(m,axis=0)%2):return None
 data=np.r_[m[:2,:11].ravel(),m[2:7,:10].ravel()]
 b=np.packbits(data).tobytes()
 if bitsword(m[2:7,10])!=sum(b)%31:return None
 if b[0] not in (0,3) or b[1]!=0 or b[2]&0x40 or not int.from_bytes(b[3:6],'big') or not int.from_bytes(b[6:9],'big'):return None
 return b
def replay(path,out,exe):
 x=np.fromfile(path,np.uint8).astype(np.float32).reshape(-1,2)-127.5
 z=x[:,0]+1j*x[:,1];del x
 z*=np.array([1,-1j,-1,1j],np.complex64)[np.arange(len(z))%4]
 y=resample_poly(z,1,5,window=firwin(241,7000,fs=240000));del z
 d=np.angle(y[1:]*np.conj(y[:-1]))*48000/(2*np.pi)
 n=240; cs=np.r_[0.,np.cumsum(d,dtype=np.float64)];sq=np.r_[0.,np.cumsum(d*d,dtype=np.float64)];var=np.maximum(sq[n:]-sq[:-n]-(cs[n:]-cs[:-n])**2/n,1)
 dib=np.array([(P['BS voice']>>(46-2*i))&3 for i in range(24)])
 template=np.repeat(np.where(dib==1,1.,-1.),10);template-=template.mean()
 score=correlate(d,template,mode='valid',method='fft')/np.sqrt(var*np.sum(template**2))
 peaks,_=find_peaks(score,height=.65,distance=1000)
 groups=[]
 for i in peaks:
  opts=[]
  for shift in range(-5,6):
   t=i+shift+5+np.arange(24)*10
   if t[0]<2 or t[-1]+2>=len(d):continue
   v=np.mean(d[t[:,None]+np.arange(-2,3)],axis=1)
   mid=(np.mean(v[v>np.median(v)])+np.mean(v[v<=np.median(v)]))/2
   dib=np.where(v>mid,1,3)
   if bitsword(np.column_stack([dib>>1,dib&1]).ravel())!=P['BS voice']:continue
   signs=np.where(dib==1,1.,-1.);amp=np.dot(v-v.mean(),signs-signs.mean())/np.sum((signs-signs.mean())**2);off=v.mean()-amp*signs.mean();err=np.mean((v-off-amp*signs)**2)/(amp**2)
   opts.append((err,int(t[0]),off,amp))
  if not opts:continue
  e,t,o,amp=min(opts)
  if e>.2:continue
  b=getburst(d,t,o,amp)
  if b is None:continue
  frames=[b];emb=[]
  for i in range(1,6):
   choices=[]
   for delta in [0,-1,1,-2,2,-3,3,-4,4]:
    bb=getburst(d,t+i*2880+delta,o,amp)
    if bb is None:continue
    center=bitsword(bb[108:156])
    if min((center^s).bit_count() for s in P.values())<=2:continue
    q=qr(np.r_[bb[108:116],bb[148:156]])
    if q is not None:choices.append((q[3],abs(delta),delta,q,bb))
   if not choices:break
   _,_,_,q,bb=min(choices,key=lambda u:u[:2]);emb.append(q);frames.append(bb)
  groups.append((t,frames,emb))
 # Strict clear-LC anchor; no audio on groups without valid, clear LC and privacy-consistent EMB.
 result=[]; accepted=0
 for t,frames,emb in groups:
  if len(emb)!=5 or len({q[0] for q in emb})!=1 or any(q[1] for q in emb) or [q[2] for q in emb]!=[1,3,3,2,0]:continue
  info=lc(frames)
  if not info:continue
  accepted+=1
  print('clear LC',path.name,round(t/48000,3),'CC',emb[0][0],info.hex(),flush=True)
  for i,b in enumerate(frames):
   payload=np.r_[b[:108],b[156:]]
   for j in range(3):result.append((t+i*2880+j*960,np.packbits(payload[j*72:(j+1)*72]).tobytes()))
 print(path.name,'sha256',hashlib.sha256(path.read_bytes()).hexdigest(),'voice syncs',len(groups),'clear groups',accepted,'frames',len(result),flush=True)
 if not result:return 0
 frames=out/(path.stem+'.frames.txt');raw=out/(path.stem+'.pcm')
 frames.write_text(''.join(f'{t} {b.hex()}\n' for t,b in result))
 subprocess.run([str(exe),str(frames),str(raw)],check=True)
 pcm=raw.read_bytes()
 assert len(pcm)==len(result)*320
 with wave.open(str(out/(path.stem+'.wav')),'wb') as w:
  w.setnchannels(1);w.setsampwidth(2);w.setframerate(8000);w.writeframes(pcm)
 a=np.frombuffer(pcm,dtype='<i2').astype(np.float64)
 print('PCM peak',np.max(np.abs(a)),'RMS',np.sqrt(np.mean(a*a)),flush=True)
 return len(result)
if __name__=='__main__':
 out=Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True)
 exe=Path(sys.argv[3]).resolve()
 for p in sorted(Path(sys.argv[1]).glob('terra*.cu8')):
  count=replay(p,out,exe)
  if 'control_only' in p.name:assert count==0
