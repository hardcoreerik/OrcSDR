#!/usr/bin/env python3
"""Offline research synchronizer: 48-kS/s CS16 or 240-kS/s +60kHz CU8.
Not the native streaming frontend. FSW fit, exact FSW, LICH parity, no expected
call IDs or voice bits. Output measured packed dibits, sample positions.
"""
import argparse, hashlib
from pathlib import Path
import numpy as np
from scipy.signal import resample_poly, firwin


def extract(path,rate,fmt):
    if fmt=='cs16':
        x=np.fromfile(path,dtype='<i2').astype(np.float64).reshape(-1,2); z=x[:,0]+1j*x[:,1]
    elif fmt=='cu8_240k_offset60k':
        x=np.fromfile(path,dtype=np.uint8).astype(np.float64).reshape(-1,2)-127.5;z=x[:,0]+1j*x[:,1]
        z*=np.array([1,-1j,-1,1j])[np.arange(len(z))%4];z=resample_poly(z,1,5,window=firwin(241,7000,fs=240000))
    else: raise ValueError(fmt)
    n=20 if rate==48 else 10
    d=np.r_[0,np.angle(z[1:]*np.conj(z[:-1]))*48000/(2*np.pi)]
    levels=np.array([1,3,-1,-3]); sync=(0xcdf59>>np.arange(18,-1,-2))&3;sv=levels[sync]; avg=sv.mean();dev=sv-avg;den=np.dot(dev,dev)
    tt=np.arange(-3*n,3*n+1)/n;a=.2
    # Raw discriminator is the tested NXDN48 acquisition path. RRC is also
    # evaluated on NXDN96; use the analytic removable-singularity limit.
    def rrc(t):
        if abs(t)<1e-12:return 1+a*(4/np.pi-1)
        if abs(abs(t)-1/(4*a))<1e-12:return a/np.sqrt(2)*((1+2/np.pi)*np.sin(np.pi/(4*a))+(1-2/np.pi)*np.cos(np.pi/(4*a)))
        return (np.sin(np.pi*t*(1-a))+4*a*t*np.cos(np.pi*t*(1+a)))/(np.pi*t*(1-(4*a*t)**2))
    h=np.array([rrc(t) for t in tt]);h/=h.sum()
    candidates=[]
    for y in ((d,) if rate==48 else (np.convolve(d,h,mode='same'),d)):
        if len(y)<200*n:continue
        # Vectorized sync evaluation without constructing a full Nx192 matrix.
        v=np.lib.stride_tricks.sliding_window_view(y,9*n+1)[:,::n];g=(v@dev)/den;m=v.mean(axis=1)-g*avg
        with np.errstate(divide='ignore',invalid='ignore'):
            err=np.sum(((v-m[:,None])/g[:,None]-sv)**2,axis=1)
        ts=np.flatnonzero((err<=2.5)&(abs(g)>=100)&(abs(g)<=2000))
        for t in ts:
            if t<3*n or t+191*n>=len(y)-3*n:continue
            vv=(y[t+np.arange(192)*n]-m[t])/g[t]; ds=np.where(vv>=2,1,np.where(vv>=0,0,np.where(vv>-2,2,3))).astype(np.uint8)
            if np.any(ds[:10]!=sync):continue
            lfsr=0xe4;lich=0;fe=0
            for j in range(10,18):
                x=int(ds[j])^((lfsr&1)*2);lfsr=(lfsr>>1)|(((lfsr^(lfsr>>4))&1)<<8);lich=lich*2+(x>>1);fe+=1-(x&1)
            if fe>1 or ((lich>>4).bit_count()&1)!=(lich&1):continue
            packed=(ds.reshape(-1,4)*np.array([64,16,4,1])).sum(axis=1).astype('uint8').tobytes().hex()
            candidates.append((float(err[t]),int(t),packed))
    best=[]
    for c in sorted(candidates):
        if not any(abs(q[1]-c[1])<3*n for q in best):best.append(c)
    return sorted(best,key=lambda q:q[1])

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rate',type=int,choices=[48,96]);p.add_argument('path',type=Path);p.add_argument('out',type=Path);p.add_argument('--format',choices=['cs16','cu8_240k_offset60k'],default='cs16');p.add_argument('--git-blob-sha1');a=p.parse_args()
    b=a.path.read_bytes();digest=hashlib.sha1(b'blob '+str(len(b)).encode()+b'\x00'+b).hexdigest()
    if a.git_blob_sha1 and digest!=a.git_blob_sha1:raise SystemExit('Input hash mismatch')
    frames=extract(a.path,a.rate,a.format);a.out.write_text(''.join(f'{t} {h}\n' for _,t,h in frames));print(a.path,'sha256',hashlib.sha256(b).hexdigest(),'git',digest,'frames',len(frames))
if __name__=='__main__':main()
