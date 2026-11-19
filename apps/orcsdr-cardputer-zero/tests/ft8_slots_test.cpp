#include "ft8_slots.hpp"
#include <array>
#include <cassert>

int main(){
  orc::zero::Ft8Slots slots;
  std::array<int16_t,120> pcm{};
  unsigned completed=0;
  auto emit=[&](uint64_t epoch,const int16_t* audio,size_t count){
    assert(epoch==45000||epoch==75000);
    assert(count==180000&&audio[0]==11&&audio[count-1]==11);
    ++completed;
  };
  pcm.fill(11);
  for(unsigned ms=31000;ms<90000;ms+=10){
    if(ms==60500){slots.clear();continue;}
    slots.offer(pcm.data(),pcm.size(),uint64_t(ms)*1000000,false,emit);
  }
  assert(completed==2);
  for(bool backward:{false,true}){
    slots.clear();pcm.fill(11);
    for(unsigned ms=45000;ms<50000;ms+=10)
      slots.offer(pcm.data(),pcm.size(),uint64_t(ms)*1000000,false,emit);
    unsigned after_jump=0;pcm.fill(22);
    const unsigned begin=backward?31000:76000,end=backward?60000:105000;
    for(unsigned ms=begin;ms<end;ms+=10)
      slots.offer(pcm.data(),pcm.size(),uint64_t(ms)*1000000,false,
        [&](uint64_t epoch,const int16_t* audio,size_t count){
          assert(epoch==(backward?45000:90000));
          assert(count==180000);
          for(size_t n=0;n<count;++n)assert(audio[n]==22);
          ++after_jump;
        });
    assert(after_jump==1);
  }
}
