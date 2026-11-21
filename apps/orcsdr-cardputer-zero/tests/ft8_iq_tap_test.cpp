#include "ft8_iq_tap.hpp"
#include <cmath>
#include <cstdio>
int main(){
  orc::zero::Ft8IqTap tap;
  orc::linux_sdr::IqBlock block;
  block.meta={7074000,960000,1,0,0,0,false};
  block.cu8.resize(16000);
  for(size_t i=0;i<8000;++i){
    const auto phase=6.283185307179586*1500*i/960000;
    block.cu8[2*i]=uint8_t(128+60*std::cos(phase));
    block.cu8[2*i+1]=uint8_t(128+60*std::sin(phase));
  }
  std::array<int16_t,128> out{};
  auto r=tap.consume(block,7074000,true,out.data(),out.size());
  if(!r.reset_slot||r.frames!=100)return 1;
  block.meta.first_sample_index=8000;
  r=tap.consume(block,7074000,true,out.data(),out.size());
  if(r.reset_slot||r.frames!=100)return 2;
  auto reference=out;
  block.meta.frequency_hz=18000000;
  for(size_t i=1;i<block.cu8.size();i+=2)block.cu8[i]=uint8_t(255-block.cu8[i]);
  if(!tap.consume(block,18000000,true,out.data(),out.size()).reset_slot)return 3;
  block.meta.first_sample_index=16000;
  r=tap.consume(block,18000000,true,out.data(),out.size());
  if(r.reset_slot||r.frames!=100||out!=reference)return 4;
  block.meta.dropped_samples_before=1;
  if(!tap.consume(block,18000000,true,out.data(),out.size()).reset_slot)return 5;
  if(tap.consume(block,18000000,true,out.data(),1).frames)return 6;
  std::puts("FT8 tap: 12 kHz USB, direct-Q inversion, gap reset, capacity check PASS");
}
