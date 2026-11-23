import numpy as np, sys
from scipy import signal
fs=2.4e6; fc=454.4e6
raw=np.memmap("wide_454p4_g30.cu8",dtype=np.uint8,mode="r")
n=len(raw)//2
taps=signal.firwin(161,100e3,fs=fs).astype(np.float32)
for fch in [float(x) for x in sys.argv[1:]]:
    shift=(fch-60e3)-fc
    out=bytearray(); zi=None; carry=0; gain=None
    CH=4_800_000
    for s in range(0,n,CH):
        e=min(n,s+CH)
        x=raw[2*s:2*e].astype(np.float32); z=((x[0::2]-127.5)+1j*(x[1::2]-127.5))/127.5
        t=(np.arange(s,e)/fs)
        z=(z*np.exp(-2j*np.pi*shift*t)).astype(np.complex64)
        y,zi=signal.lfilter(taps,1.0,z,zi=zi) if zi is not None else signal.lfilter(taps,1.0,z,zi=np.zeros(len(taps)-1,dtype=np.complex64))
        y=y[carry::10]; carry=(carry+len(z))%10  # decimate by 10 keeping phase across chunks
        carry=(10-((len(z)-carry)%10))%10 if False else carry
        if gain is None: gain=0.25/max(1e-6,float(np.sqrt(np.mean(np.abs(y)**2))))   # fixed per file: rms ~0.25 of full scale
        q=np.empty(2*len(y),dtype=np.uint8)
        q[0::2]=np.clip(127.5+127.5*gain*y.real,0,255)
        q[1::2]=np.clip(127.5+127.5*gain*y.imag,0,255)
        out+=q.tobytes()
    name=f"cap_wide_{fch/1e6:.4f}".replace(".","p")+".cu8"
    open(name,"wb").write(out); print(name,len(out))
