#pragma once
#include "acars_dashboard.hpp"
#include "acars_dashboard_model.hpp"
#include "ft8_tuning.hpp"
#include <algorithm>
#include <string>
namespace orc::zero {
struct AcarsTuning {
  uint32_t requested=acars_dashboard::kDefaultChannelHz;
  orcsdr::acars_ui::Region region=orcsdr::acars_ui::Region::north_america;
  size_t preset_index=0;
  uint32_t custom_hz=0;
  bool editing=false;
  std::string entry,error;
  bool input(Key key,char digit){
    if(!editing){
      if(key==Key::frequency){editing=true;entry.clear();error.clear();return true;}
      if(key==Key::step){region=orcsdr::acars_ui::Region((uint8_t(region)+1)%uint8_t(orcsdr::acars_ui::Region::count));preset_index=0;requested=orcsdr::acars_ui::preset(region,0).frequency_hz;return true;}
      if(key!=Key::left&&key!=Key::right)return false;
      const size_t count=orcsdr::acars_ui::preset_count(region);
      preset_index=key==Key::left?(preset_index?preset_index-1:count-1):(preset_index+1)%count;
      requested=orcsdr::acars_ui::preset(region,preset_index).frequency_hz;
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
        requested=hz;custom_hz=hz;editing=false;error.clear();
      }else error="118-137 MHZ ON A 25 KHZ GRID";
    }
    return true;
  }
};
}
