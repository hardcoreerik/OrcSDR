#include "ft8_slots.hpp"
#include <array>
#include <cassert>
#include <iostream>

int main(){
  orc::zero::Ft8Slots slots;
  assert(!slots.set_period(0)&&!slots.set_period(10000));
  assert(slots.set_period(7500));
  std::array<int16_t,120> pcm;pcm.fill(42);
  unsigned complete=0;
  for(unsigned ms=31000;ms<60000;ms+=10){
    if(ms==46000){slots.clear();continue;}
    slots.offer(pcm.data(),pcm.size(),uint64_t(ms)*1000000,false,
      [&](uint64_t epoch,const int16_t* samples,size_t count){
        assert(epoch==37500||epoch==52500);
        assert(count==90000&&samples[0]==42&&samples[count-1]==42);
        ++complete;
      });
  }
  assert(complete==2);
  // A mode change must discard a partial FT4 slot.
  assert(slots.set_period(15000));complete=0;
  for(unsigned ms=61000;ms<90000;ms+=10)
    slots.offer(pcm.data(),pcm.size(),uint64_t(ms)*1000000,false,
      [&](uint64_t epoch,const int16_t*,size_t count){assert(epoch==75000&&count==180000);++complete;});
  assert(complete==1);
  std::cout<<"FT4 7.5-second slots, partial-slot discard, gaps and FT8 mode reset PASS\n";
}
