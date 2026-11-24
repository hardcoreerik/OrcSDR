#!/usr/bin/env python3
"""Offline DMR voice proof. This is not the native streaming receiver. No DSD-FME code or reference frames are used for extraction or synthesis."""
from pathlib import Path
import argparse, ctypes as C, hashlib, re, zipfile, difflib, json, subprocess, wave
import numpy as np
from scipy.signal import resample_poly, firwin, correlate, find_peaks
P={'BS voice':0x755FD7DF75F7,'BS data':0xDFF57D75DF5D,'MS voice':0x7F7D5DD57DFD,'MS data':0xD5D7F77FD757,'TS1 voice':0x5D577F7757FF,'TS2 voice':0x7DFFD5F55D5F,'TS1 data':0xF7FDD5DDFD55,'TS2 data':0xD7557F5FF7F5}
# Systematic QR(16,7,6), row parity values per DMR standard.
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
 # t is first sync symbol center; 54 symbols of payload precede center.
 pos=t+np.arange(-54,78)*10
 if pos[0]<2 or pos[-1]+2>=len(d):return None
 v=d[pos]-off
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
 
 return b
# CACH Hamming (7,4,3), reject corrected hints for slot assignment.
def cach(d,t,o,a):
 pos=t+np.arange(-66,-54)*10
 if pos[0]<0:return None
 v=d[pos]-o;dib=np.argmin(abs(v[:,None]-np.array([a/3,a,-a/3,-a])),axis=1)
 bits=np.column_stack([dib>>1,dib&1]).ravel();b=bits[[0,4,8,12,14,18,22]]
 p,q,r,s,h,j,k=map(int,b)
 if (p^q^r^h)|(q^r^s^j)|(p^q^s^k):return None
 return q+1
# Full link control, based on existing OrcSDR D1 algorithms, including independent RS check.
gpar=[0x3da,0xd99,0x6cd,0x367,0xdc6,0xa97,0x93e,0x8eb]
gwords=[]
for v in range(256):
 w=v<<12
 for j in range(8):
  if v&(128>>j):w^=gpar[j]
 gwords.append(w)
def gf(x,y):
 p=0
 for i in range(8):
  if y&1:p^=x
  x=((x<<1)^(0x1d if x&128 else 0))&255;y>>=1
 return p
def syndrome(row,par):
 s=0
 for b,p in zip(row,par):
  if b:s^=p
 return s
def repair(row,par):
 s=syndrome(row,par)
 if s:
  if s not in par:return False
  row[par.index(s)]^=1
 return True
rpar=[9,13,15,14,7,10,5,11,12,6,3,8,4,2,1];cpar=[15,14,7,10,5,11,12,6,3,8,4,2,1]
def full_lc(b):
 w=bitsword(np.r_[b[98:108],b[156:166]]);ds=[(w^x).bit_count() for x in gwords];m=min(ds)
 if m>3 or ds.count(m)!=1:return None
 v=ds.index(m);cc,typ=v>>4,v&15
 if typ not in (0,1,2):return (cc,typ,None)
 tx=np.r_[b[:98],b[166:]];enc=tx[np.arange(196)*181%196];mat=enc[1:].reshape(13,15).copy()
 for _ in range(3):
  if not all(repair(row,rpar) for row in mat[:9]):return (cc,typ,None)
  if not all(repair(mat[:,i],cpar) for i in range(15)):return (cc,typ,None)
 if any(syndrome(row,rpar) for row in mat[:9]) or any(syndrome(mat[:,i],cpar) for i in range(15)):return (cc,typ,None)
 if enc[0] or np.any(mat[0,:3]):return (cc,typ,None)
 data=np.packbits(mat[:9,:11].ravel()[3:]).tobytes()
 if typ==0:return (cc,typ,None) # PI header always breaks eligibility; never decode protected content
 coded=data[:9]+bytes(x^(0x96 if typ==1 else 0x99) for x in data[9:]);
 for root in (2,4,8):
  s=0
  for b in coded:s=gf(s,root)^b
  if s:return (cc,typ,None)
 return cc,typ,data[:9]
def identity(b):
 if b is None or b[0] not in (0,3) or b[1] or b[2]&0x40:return None
 src=int.from_bytes(b[6:9],'big');dst=int.from_bytes(b[3:6],'big')
 return (src,dst) if src and dst else None
Mat=(C.c_char*24)*4; Dat=C.c_char*49
lib=None
rw=[0,1]*11+[0,2]*7; rx=[23,10,22,9,21,8,20,7,19,6,18,5,17,4,16,3,15,2,14,1,13,0,12,10,11,9,10,8,9,7,8,6,7,5,6,4];ry=[0,2,0,2,0,2,0,2,0,3,0,3]+[1,3]*12;rz=[5,3,4,2,3,1,2,0,1,13,0,12,22,11,21,10,20,9,19,8,18,7,17,6,16,5,15,4,14,3,13,2,12,1,11,0]
def params(w):
 m=Mat();data=Dat()
 for i in range(36):
  d=(w[i//4]>>(6-2*(i%4)))&3;m[rw[i]][rx[i]]=bytes([d>>1]);m[ry[i]][rz[i]]=bytes([d&1])
 a=lib.mbe_eccAmbe3600x2450C0(C.byref(m));lib.mbe_demodulateAmbe3600x2450Data(C.byref(m));b=lib.mbe_eccAmbe3600x2450Data(C.byref(m),C.byref(data));return np.packbits(np.frombuffer(bytes(data),dtype=np.uint8)).tobytes().hex().upper(),a,a+b

def inspect(d,x):
 groups=[];data=[]
 for t,n,o,amp,e in zip(x['time'],x['name'],x['offset'],x['amp'],x['err']):
  if e>.2:continue
  b=getburst(d,t,o,amp)
  if b is None:continue
  slot=cach(d,t,o,amp)
  if n=='BS data':
   f=full_lc(b)
   data.append((int(t),slot,*(f if f else (None,None,None))))
   continue
  if n!='BS voice':continue
  frames=[b];emb=[];slots=[slot]
  for i in range(1,6):
   opts=[]
   for delta in [0,-1,1,-2,2,-3,3,-4,4]:
    tt=t+i*2880+delta;bb=getburst(d,tt,o,amp)
    if bb is None:continue
    center=bitsword(bb[108:156])
    if min((center^s).bit_count() for s in P.values())<=2:continue
    q=qr(np.r_[bb[108:116],bb[148:156]])
    if q is not None:opts.append((q[3],abs(delta),delta,q,bb,cach(d,tt,o,amp)))
   if not opts:break
   _,_,_,q,bb,ss=min(opts,key=lambda u:u[:2]);emb.append(q);frames.append(bb);slots.append(ss)
  info=lc(frames) if len(emb)>=4 and [q[2] for q in emb[:4]]==[1,3,3,2] else None
  groups.append(dict(t=int(t),frames=frames,emb=emb,slots=slots,info=info))
 return groups,data

def acquire(path):
    # Only known capture format is accepted. Do not guess the tuning offset from filenames.
    raw=np.fromfile(path,np.uint8)
    if len(raw)%2: raise ValueError('odd CU8 byte count')
    x=raw.astype(np.float32).reshape(-1,2)-127.5
    z=x[:,0]+1j*x[:,1]; del x
    z*=np.array([1,-1j,-1,1j],np.complex64)[np.arange(len(z))%4]
    y=resample_poly(z,1,5,window=firwin(241,7000,fs=240000)); del z
    d=np.angle(y[1:]*np.conj(y[:-1]))*48000/(2*np.pi)
    t=np.arange(-30,31)/10; a=.2
    h=np.array([1+a*(4/np.pi-1) if abs(q)<1e-9 else (np.sin(np.pi*(1-a)*q)+4*a*q*np.cos(np.pi*(1+a)*q))/(np.pi*q*(1-16*a*a*q*q)) for q in t])
    d=np.convolve(d,h/h.sum(),mode='same')
    n=240; cs=np.r_[0.,np.cumsum(d,dtype=np.float64)];sq=np.r_[0.,np.cumsum(d*d,dtype=np.float64)];var=np.maximum(sq[n:]-sq[:-n]-(cs[n:]-cs[:-n])**2/n,1)
    candidates=[]
    for name in ('BS voice','BS data'):
        p=P[name]; dib=np.array([(p>>(46-2*i))&3 for i in range(24)])
        signs=np.where(dib==1,1.,-1.);template=np.repeat(signs,10);template-=template.mean()
        score=correlate(d,template,mode='valid',method='fft')/np.sqrt(var*np.sum(template**2))
        # Keep positive and negative extrema as in the initial experiment, then require exact measured positive polarity.
        peaks,_=find_peaks(np.abs(score),height=.65,distance=1000)
        for i in peaks:
            if score[i]<.65:continue
            opts=[]
            for shift in range(-5,6):
                tt=i+shift+5+np.arange(24)*10
                if tt[0]<2 or tt[-1]+2>=len(d):continue
                v=d[tt];mid=(np.mean(v[v>np.median(v)])+np.mean(v[v<=np.median(v)]))/2
                measured=np.where(v>mid,1,3)
                if bitsword(np.column_stack([measured>>1,measured&1]).ravel())!=p:continue
                amp=np.dot(v-v.mean(),signs-signs.mean())/np.sum((signs-signs.mean())**2); off=v.mean()-amp*signs.mean()
                if amp<=0:continue
                err=np.mean((v-off-amp*signs)**2)/amp**2;opts.append((err,int(tt[0]),off,amp))
            if opts:
                e,tt,o,g=min(opts);candidates.append((tt,name,o,g,e))
    candidates.sort()
    return d,{k:np.array([row[i] for row in candidates]) for i,k in enumerate(('time','name','offset','amp','err'))}


def gate(groups,data):
    """Offline call continuity. State is slot-local, expires, and is never created by alias or PI-clear alone."""
    state={};accepted=[]
    for g in groups:
        t=g['t'];emb=g['emb'];slots=g['slots'];info=g['info'];ss=[s for s in slots if s is not None]
        slot=ss[0] if len(ss)>=5 and len(set(ss))==1 else None
        if slot is None or len(emb)!=5 or len({q[0] for q in emb})!=1 or any(q[1] for q in emb):
            if slot:state.pop(slot,None)
            else:state.clear()
            continue
        cc=emb[0][0];old=state.get(slot)
        # Every prior same-slot data sync is a boundary, including unknown slot at matching cadence.
        # Only an independently validated, clear, immediately preceding voice header can open a new call.
        previous=[(dt,sl,c,typ,b) for dt,sl,c,typ,b in data if dt<t and (not old or dt>old['t']) and (sl==slot or (sl is None and abs((t-dt+1440)%2880-1440)<=5))]
        continuity=bool(old and abs(t-old['t']-17280)<=5 and old['cc']==cc and t-old['anchor']<=96000 and not previous)
        hdr=previous[-1] if previous else None
        header_id=identity(hdr[4]) if hdr and hdr[1]==slot and hdr[2]==cc and hdr[3]==1 and abs(t-hdr[0]-2880)<=5 else None
        ident=identity(info)
        if info is not None and ((info[0]&0x80) or ((info[0]&0x3f) in (0,3) and not ident)):
            state.pop(slot,None);continue
        if hdr and hdr[3]==0 and abs(t-hdr[0]-2880)<=5:
            state.pop(slot,None);continue
        if ident and header_id and ident!=header_id:
            state.pop(slot,None);continue
        if ident:
            anchor=t
        elif header_id:
            ident=header_id;anchor=hdr[0]
        elif continuity:
            ident=old['id'];anchor=old['anchor']
        else:
            state.pop(slot,None);continue
        epoch=old['epoch'] if continuity and old['id']==ident else t
        state[slot]={'t':t,'cc':cc,'id':ident,'anchor':anchor,'epoch':epoch}
        accepted.append((g,slot,cc,ident,epoch))
    return accepted


def main():
    global lib
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('iq_dir',type=Path);ap.add_argument('out',type=Path);ap.add_argument('cli',type=Path);ap.add_argument('library',type=Path)
    ap.add_argument('--reference',type=Path,help='optional external reference ZIP, comparison only')
    args=ap.parse_args();args.out.mkdir(parents=True,exist_ok=True);lib=C.CDLL(str(args.library.resolve()));refs=zipfile.ZipFile(args.reference) if args.reference else None
    report={};files=sorted(args.iq_dir.glob('*.cu8'))
    if not files:raise SystemExit('No CU8 inputs')
    for p in files:
        d,sync=acquire(p);groups,data=inspect(d,sync);accepted=[];details=[]
        for g,slot,cc,ident,epoch in gate(groups,data):
            t=g['t'];info=g['info'];details.append((round(t/48000,3),slot,cc,ident,epoch,info.hex() if info else None))
            for i,b in enumerate(g['frames']):
                payload=np.r_[b[:108],b[156:]]
                for j in range(3):
                    w=np.packbits(payload[j*72:(j+1)*72]).tobytes();param,a,e=params(w);accepted.append((t+i*2880+j*960,slot,cc,ident,epoch,w,param,a,e))
        key='call1' if 'call_cc13' in p.name else 'call2' if '152p685_call' in p.name else 'call3' if '153p068_call' in p.name else None
        ref=[];matches=None
        if refs and key:
            refname=next(x for x in refs.namelist() if '/'+key+'_' in x);ref=re.findall(r'AMBE ([0-9A-F]{14}) err = \[(\d+)\] \[(\d+)\]',refs.read(refname).decode());seq=[x[6] for x in accepted];rs=[r[0] for r in ref]
            matches=sum(b.size for b in difflib.SequenceMatcher(None,rs,seq,autojunk=False).get_matching_blocks())
        r={'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'accepted_frames':len(accepted),'reference_frames':len(ref) if refs and key else None,'exact_aligned_matches':matches,'legacy_data_bit_repairs':sum(x[8] for x in accepted),'groups':details,'audio':[]};report[p.name]=r
        print(p.name,'sha256',r['sha256'],'accepted',len(accepted),'ref',r['reference_frames'],'aligned matches',matches,flush=True)
        if 'control_only' in p.name and accepted:raise RuntimeError('false audio on control-only capture')
        for slot,cc,ident,epoch in sorted(set((x[1],x[2],x[3],x[4]) for x in accepted)):
            aa=[x for x in accepted if (x[1],x[2],x[3],x[4])==(slot,cc,ident,epoch)]
            stem=f'{p.stem}_ts{slot}_src{ident[0]}_at{epoch}';txt=args.out/(stem+'.frames.txt');raw=args.out/(stem+'.pcm');txt.write_text(''.join(f'{x[0]} {x[5].hex()}\n' for x in aa))
            proc=subprocess.run([str(args.cli.resolve()),str(txt),str(raw)],check=True,capture_output=True,text=True);print(stem,proc.stderr.strip(),flush=True)
            pcm=np.fromfile(raw,dtype='<i2').reshape(-1,160);wav=[];last=None
            for x,fr in zip(aa,pcm):
                if last is not None:
                    gap=max(0,round((x[0]-last)/960)-1);wav.extend([np.zeros(160,dtype='<i2')]*gap)
                wav.append(fr);last=x[0]
            samples=np.concatenate(wav).astype('<i2');a=samples.astype(float)
            dest=args.out/(stem+'.wav')
            with wave.open(str(dest),'wb') as w:w.setnchannels(1);w.setsampwidth(2);w.setframerate(8000);w.writeframes(samples.tobytes())
            raw.unlink();(args.out/(stem+'.parameters.txt')).write_text(''.join(f'{x[0]} {x[6]} {x[7]} {x[8]}\n' for x in aa))
            r['audio'].append({'file':dest.name,'frames':len(aa),'duration_s':len(a)/8000,'rms':float(np.sqrt(np.mean(a*a))),'peak':int(np.max(abs(a))),'clipped_samples':int(np.sum(abs(a)>=32760)),'zero_frames':int(np.sum(np.all(samples.reshape(-1,160)==0,axis=1))),'codec_log':proc.stderr.strip()})
    (args.out/'results.json').write_text(json.dumps(report,indent=2));print('PASS: replay completed; listening and native receiver integration pending',flush=True)
if __name__=='__main__':main()
