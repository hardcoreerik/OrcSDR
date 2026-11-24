#include "orcsdr_vocoder.h"
#include <string.h>
// DMR AMBE interleave schedule, from szechyjs/dsd include/dmr_const.h.
// Copyright (C) 2010 DSD Author. ISC notice in THIRD_PARTY.md.
static const unsigned char rw[36]={0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,2,0,2,0,2,0,2,0,2,0,2,0,2};
static const unsigned char rx[36]={23,10,22,9,21,8,20,7,19,6,18,5,17,4,16,3,15,2,14,1,13,0,12,10,11,9,10,8,9,7,8,6,7,5,6,4};
static const unsigned char ry[36]={0,2,0,2,0,2,0,2,0,3,0,3,1,3,1,3,1,3,1,3,1,3,1,3,1,3,1,3,1,3,1,3,1,3,1,3};
static const unsigned char rz[36]={5,3,4,2,3,1,2,0,1,13,0,12,22,11,21,10,20,9,19,8,18,7,17,6,16,5,15,4,14,3,13,2,12,1,11,0};
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
    mbe_processAmbe3600x2450Frame(pcm,&c0,&total,errors,matrix,data,&v->current,&v->previous,&v->enhanced,1);
    if (result) {result->corrected=c0;result->total=total;memcpy(result->flags,errors,sizeof(errors));}
    return 1;
}
