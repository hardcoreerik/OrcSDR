#include "orcsdr_vocoder.h"
#include <string.h>
#include <math.h>
// DMR AMBE interleave schedule, from szechyjs/dsd include/dmr_const.h.
// Copyright (C) 2010 DSD Author. ISC notice in THIRD_PARTY.md.
static const unsigned char rw[36]={0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,2,0,2,0,2,0,2,0,2,0,2,0,2};
static const unsigned char rx[36]={23,10,22,9,21,8,20,7,19,6,18,5,17,4,16,3,15,2,14,1,13,0,12,10,11,9,10,8,9,7,8,6,7,5,6,4};
static const unsigned char ry[36]={0,2,0,2,0,2,0,2,0,3,0,3,1,3,1,3,1,3,1,3,1,3,1,3,1,3,1,3,1,3,1,3,1,3,1,3};
static const unsigned char rz[36]={5,3,4,2,3,1,2,0,1,13,0,12,22,11,21,10,20,9,19,8,18,7,17,6,16,5,15,4,14,3,13,2,12,1,11,0};
// Use the existing pinned mbelib Golay generator, not another table copy.
extern const int golayGenerator[12];
static unsigned weight(unsigned v) { unsigned n=0; while(v){n+=v&1u;v>>=1;} return n; }
static unsigned code23(const char *data) {
    unsigned d=0,parity=0;
    for(unsigned i=0;i<12;i++){d=(d<<1)|(unsigned)data[i];if(data[i])parity^=(unsigned)golayGenerator[i];}
    return (d<<11)|parity;
}
static unsigned rowword(const char *row,unsigned n) {
    unsigned v=0;while(n){v=(v<<1)|(unsigned)row[--n];}return v;
}
void orc_vocoder_reset(orc_vocoder *v) {
    if (v) { memset(v,0,sizeof(*v)); mbe_initMbeParms(&v->current,&v->previous,&v->enhanced); }
}
int orc_vocoder_dmr(orc_vocoder *v,const uint8_t wire[9],enum orc_privacy privacy,int16_t pcm[160],orc_vocoder_result *result) {
    if (result) memset(result,0,sizeof(*result));
    if (!pcm) return 0;
    memset(pcm,0,160*sizeof(*pcm));
    if (!v || !wire || privacy!=ORC_PRIVACY_CLEAR) { orc_vocoder_reset(v); return 0; }
    char matrix[4][24]={{0}}, data[49]={0}, errors[64]={0};
    int c0=0,total=0;
    for (unsigned i=0;i<36;i++) {
        unsigned d=(wire[i/4]>>(6-2*(i%4)))&3;
        matrix[rw[i]][rx[i]]=(char)(d>>1); matrix[ry[i]][rz[i]]=(char)(d&1);
    }
    const unsigned c0wire=rowword(matrix[0],24);
    (void)mbe_eccAmbe3600x2450C0(matrix);
    mbe_demodulateAmbe3600x2450Data(matrix);
    const unsigned c1wire=rowword(matrix[1],23);
    (void)mbe_eccAmbe3600x2450Data(matrix,data);
    const unsigned c0code=code23(data);
    c0=(int)weight(c0wire ^ ((c0code<<1)|(weight(c0code)&1u)));
    total=c0+(int)weight(c1wire ^ code23(data+12));
    // Extended Golay C0 detects a fourth error; do not synthesize a miscorrected pitch/gain.
    if(c0>3){orc_vocoder_reset(v);strcpy(errors,"E");}
    else {
        unsigned b0=((unsigned)data[0]<<6)|((unsigned)data[1]<<5)|((unsigned)data[2]<<4)|((unsigned)data[3]<<3)|((unsigned)data[37]<<2)|((unsigned)data[38]<<1)|(unsigned)data[39];
        float buf[160]={0};
        mbe_processAmbe2450Dataf(buf,&c0,&total,errors,data,&v->current,&v->previous,&v->enhanced,1);
        // Standard AMBE silence types are intentionally quiet on this receiver.
        // Process the codec parameters above for predictor continuity; mute PCM only.
        if(b0==124 || b0==125){size_t n=strlen(errors);errors[n]='S';errors[n+1]=0;}
        else {
            float peak=0.0f; int valid=1;
            for(unsigned i=0;i<160;i++){if(!isfinite(buf[i])){valid=0;break;}float a=fabsf(buf[i]);if(a>peak)peak=a;}
            if(!valid){orc_vocoder_reset(v);strcpy(errors,"E");}
            else {const float gain=peak>8000.0f?12000.0f/peak:1.5f;
                for(unsigned i=0;i<160;i++)pcm[i]=(int16_t)(buf[i]*gain);}
        }
    }
    if (result) {result->corrected=c0;result->total=total;memcpy(result->flags,errors,sizeof(errors));}
    return 1;
}
