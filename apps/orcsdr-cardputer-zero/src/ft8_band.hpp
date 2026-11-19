#pragma once
#include "ft8_model.hpp"
namespace orc::zero {
struct Ft8BandSelection {
  size_t index=orcsdr::ft8::nearest_band(7074000);
  void step(bool forward){
    const auto count=orcsdr::ft8::band_count();
    index=(index+(forward?1:count-1))%count;
  }
  uint32_t dial()const{return orcsdr::ft8::band(index)->dial_hz;}
};
}
