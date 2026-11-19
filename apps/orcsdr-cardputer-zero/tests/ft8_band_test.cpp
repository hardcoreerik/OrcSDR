#include "ft8_band.hpp"
#include <cassert>
#include <iostream>
int main(){
  orc::zero::Ft8BandSelection b;
  assert(b.dial()==7074000);
  const auto initial=b.index;
  const auto count=orcsdr::ft8::band_count();
  for(size_t n=0;n<count;++n){
    assert(b.dial()==orcsdr::ft8::band(b.index)->dial_hz);
    auto previous=b.index;b.step(true);b.step(false);assert(b.index==previous);
    b.step(true);
  }
  assert(b.index==initial);
  b.index=0;b.step(false);assert(b.index==count-1);
  b.step(true);assert(b.index==0);
  std::cout<<"FT8 shared band presets and wrap PASS\n";
}
