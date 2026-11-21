#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace orc::zero {

// Platform-neutral control-channel survey state. Storage/catalog ownership stays
// with the host until the Zero has an authenticated profile route.
struct P25ProfileScan {
  static constexpr size_t kMaxChannels = 8;
  static constexpr uint32_t kSettleMs = 750;
  std::array<uint32_t, kMaxChannels> channels{};
  std::array<float, kMaxChannels> levels{};
  size_t count=0, selected=0, cursor=0;
  uint32_t next_ms=0;
  bool active=false, waiting=false;
  void clear(){channels.fill(0);levels.fill(-120.0f);count=selected=cursor=0;next_ms=0;active=waiting=false;}
  bool set_channels(const uint32_t* values,size_t size){
    clear(); if(!values)return false;
    for(size_t i=0;i<size&&count<kMaxChannels;++i){
      if(values[i]<24000000u||values[i]>1766000000u)continue;
      bool duplicate=false;for(size_t j=0;j<count;++j)duplicate|=channels[j]==values[i];
      if(!duplicate)channels[count++]=values[i];
    } return count!=0;
  }
  void start(uint32_t now_ms){if(!count)return;levels.fill(-120.0f);cursor=0;active=true;waiting=false;next_ms=now_ms;}
  void stop(){active=waiting=false;} bool running()const{return active;}
  uint32_t service(uint32_t now_ms,bool locked,float level_dbfs){
    if(!active||now_ms<next_ms)return 0;
    if(waiting){levels[cursor]=locked?std::max(level_dbfs,-119.0f):level_dbfs;waiting=false;
      if(++cursor>=count){selected=0;for(size_t i=1;i<count;++i)if(levels[i]>levels[selected])selected=i;active=false;return channels[selected];}}
    waiting=true;next_ms=now_ms+kSettleMs;return channels[cursor];
  }
  bool self_check()const{return count<=kMaxChannels&&selected<(count?count:1)&&cursor<=count;}
};

}  // namespace orc::zero
