import numpy as np, sys
fn=sys.argv[1]; centre=float(sys.argv[2])*1e6; fs=240000
raw=np.fromfile(fn,dtype=np.uint8).astype(np.float32); z=(raw[0::2]-127.5)+1j*(raw[1::2]-127.5); z/=127.5
print("samples",len(z),"clip frac",float(np.mean((raw==0)|(raw==255))))
N=4096; win=np.hanning(N); nseg=len(z)//N
S=np.zeros(N); mx=np.zeros(N)
segs=z[:nseg*N].reshape(nseg,N)*win
P=np.abs(np.fft.fftshift(np.fft.fft(segs,axis=1),axes=1))**2
avg=10*np.log10(P.mean(0)+1e-12); peak=10*np.log10(P.max(0)+1e-12)
f=(np.arange(N)-N//2)*fs/N+centre
med=np.median(avg); print("floor(med) %.1f dB"%med)
# duty: fraction of segments where each 12.5 kHz window exceeds floor+10 dB
bw=int(12500/(fs/N)); 
for fc in sorted(set(np.round(f[avg>med+6]/12500)*12500)):
    idx=np.abs(f-fc)<=6250
    band=10*np.log10(P[:,idx].sum(1)+1e-12); on=np.mean(band>np.median(band)+8)
    print(f"{fc/1e6:10.4f} MHz  avg +{avg[idx].max()-med:5.1f} dB  max-seg +{band.max()-np.median(band):5.1f} dB  active-segments {100*on:5.1f}%")
