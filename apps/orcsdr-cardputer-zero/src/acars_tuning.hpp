#pragma once
#include "acars_dashboard.hpp"
#include "ft8_tuning.hpp"
#include <algorithm>
#include <string>
namespace orc::zero {
struct AcarsTuning {
  uint32_t requested=acars_dashboard::kDefaultChannelHz;
  bool editing=false;
  std::string entry,error;
  bool input(Key key,char digit){
    if(!editing){
      if(key==Key::frequency){editing=true;entry.clear();error.clear();return true;}
      if(key!=Key::left&&key!=Key::right)return false;
      requested=uint32_t(std::clamp<int64_t>(int64_t(requested)+(key==Key::left?-25000:25000),118000000,137000000));
      return true;
    }
    if(key==Key::escape){editing=false;error.clear();}
    else if(key==Key::backspace){if(!entry.empty())entry.pop_back();}
    else if(key==Key::digit||key==Key::point){
      char b[20]{};std::snprintf(b,sizeof(b),"%s",entry.c_str());
      orcsdr::ft8::tuning::entry_key(b,sizeof(b),key==Key::point?'.':digit);entry=b;
    }else if(key==Key::enter){
      uint32_t hz=0;acars_dashboard::RadioRequest request;
      if(orcsdr::ft8::tuning::parse_mhz(entry.c_str(),&hz)&&acars_dashboard::make_radio_request(hz,request)){
        requested=hz;editing=false;error.clear();
      }else error="118-137 MHZ ON A 25 KHZ GRID";
    }
    return true;
  }
};
}
