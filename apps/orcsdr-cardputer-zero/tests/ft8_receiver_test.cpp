#include "ft8_receiver.hpp"
#include <cstdio>
int main(){
  orc::zero::Ft8Receiver receiver;
  if(!receiver.begin())return 1;
  orc::linux_sdr::IqBlock block;
  block.meta={7074000,240000,1,0,0,0,false};
  block.cu8.assign(4800,128);
  unsigned slots=0;
  bool valid=true;
  for(unsigned i=0;i<3000;++i){
    block.meta.first_sample_index=uint64_t(i)*2400;
    block.meta.discontinuity=i==1600;
    if(!receiver.consume(block,7074000,true,30000000000ull+i*10000000ull,
      [&](uint64_t utc,const orcsdr::ft8::Decode*,size_t count){
        ++slots;
        valid &= utc==30000&&count==0;
      }))return 2;
  }
  if(!valid||slots!=1)return 3;
  std::puts("FT8 native backend processed one complete silent slot; gap slot skipped");
}
