#include "orcsdr_vocoder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv) {
    if(argc!=3) return 2;
    FILE *in=fopen(argv[1],"r"),*out=fopen(argv[2],"wb");
    if(!in||!out) return 3;
    orc_vocoder v; orc_vocoder_reset(&v); srand(1);
    char line[128], hex[19]; unsigned long t=0,last=0; unsigned n=0,errors=0,muted=0;
    while(fgets(line,sizeof(line),in)) {
        if(sscanf(line,"%lu %18s",&t,hex)!=2||strlen(hex)!=18) return 4;
        uint8_t wire[9]; for(unsigned i=0;i<9;i++){ unsigned x; if(sscanf(hex+2*i,"%2x",&x)!=1)return 5; wire[i]=(uint8_t)x; }
        if(n && (t<=last || t-last>1000)) orc_vocoder_reset(&v);
        int16_t pcm[160]; orc_vocoder_result r;
        if(!orc_vocoder_dmr(&v,wire,ORC_PRIVACY_CLEAR,pcm,&r))return 6;
        errors+=(unsigned)r.total;
        if(strchr(r.flags,'E')||strchr(r.flags,'M')||strchr(r.flags,'T'))muted++;
        // Write little-endian explicitly.
        for(unsigned i=0;i<160;i++){uint16_t s=(uint16_t)pcm[i]; fputc(s&255,out);fputc(s>>8,out);}
        n++; last=t;
    }
    fprintf(stderr,"frames=%u corrections=%u codec_silence_flags=%u\n",n,errors,muted);
    fclose(in);if(fclose(out)!=0)return 7;return 0;
}
