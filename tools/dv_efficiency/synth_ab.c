/* Test shared synthesis using deterministic valid-bounded parameter states.
 * Includes changing voiced/unvoiced, rising/falling harmonic counts and supported qualities. */
#include "mbelib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned rnd=0x81957e71u;
static unsigned next(void){rnd^=rnd<<13;rnd^=rnd>>17;rnd^=rnd<<5;return rnd;}
static float unit(void){return (float)(next()&0xffffu)/65535.f;}
static void emit(FILE*f,const void*p,size_t n){if(fwrite(p,1,n,f)!=n)exit(4);}
int main(int argc,char**argv){if(argc!=2)return 2;FILE*f=fopen(argv[1],"wb");if(!f)return 3;const int qualities[]={0,1,2,3,8,32,64,65};for(unsigned q=0;q<sizeof qualities/sizeof qualities[0];q++){srand(9001+q);mbe_parms cur={0},prev={0},enh={0};mbe_initMbeParms(&cur,&prev,&enh);for(unsigned frame=0;frame<96;frame++){cur.L=9+next()%48;cur.w0=3.0f/(float)cur.L;for(int l=1;l<=cur.L;l++){cur.Ml[l]=unit()*8.f;cur.Vl[l]=(int)(next()&1);}float pcm[160];mbe_synthesizeSpeechf(pcm,&cur,&prev,qualities[q]);emit(f,pcm,sizeof pcm);emit(f,&cur,sizeof cur);emit(f,&prev,sizeof prev);mbe_moveMbeParms(&cur,&prev);}int next_rand=rand();emit(f,&next_rand,sizeof next_rand);}return fclose(f)!=0;}
