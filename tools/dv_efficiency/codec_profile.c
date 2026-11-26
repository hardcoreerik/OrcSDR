/* Host benchmark: repeated replay of native receiver's actual wire traces. */
#include "orcsdr_vocoder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define MAX 20000
struct frame {uint8_t wire[9];int reset;};
static struct frame frames[MAX];
int main(int argc,char**argv){if(argc<3)return 2;int loops=atoi(argv[1]);if(loops<=0)return 2;size_t count=0;for(int a=2;a<argc;a++){FILE*f=fopen(argv[a],"r");if(!f)return 3;char line[64];int first=1;while(fgets(line,sizeof(line),f)){if(strlen(line)<18||count==MAX)return 4;frames[count].reset=first;first=0;for(int i=0;i<9;i++){unsigned x;if(sscanf(line+2*i,"%2x",&x)!=1)return 5;frames[count].wire[i]=(uint8_t)x;}count++;}fclose(f);}if(!count)return 6;uint64_t hash=14695981039346656037ull;clock_t t=clock();for(int k=0;k<loops;k++){orc_vocoder v;srand(42);for(size_t i=0;i<count;i++){if(frames[i].reset)orc_vocoder_reset(&v);int16_t pcm[160];orc_vocoder_result r;if(!orc_vocoder_dmr(&v,frames[i].wire,ORC_PRIVACY_CLEAR,pcm,&r))return 7;const unsigned char *p=(unsigned char*)pcm;for(size_t j=0;j<sizeof pcm;j++){hash^=p[j];hash*=1099511628211ull;}p=(unsigned char*)&r;for(size_t j=0;j<sizeof r;j++){hash^=p[j];hash*=1099511628211ull;}}hash^=(unsigned)rand();hash*=1099511628211ull;}
double us=1000000.*(clock()-t)/CLOCKS_PER_SEC;printf("frames=%zu iterations=%d cpu_ms=%.3f us/frame=%.3f digest=%016llx codec_bytes=%zu\n",count,loops,us/1000.,us/(count*(double)loops),(unsigned long long)hash,sizeof(orc_vocoder));return 0;}
