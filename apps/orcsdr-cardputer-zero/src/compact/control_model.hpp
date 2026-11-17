#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace orc::zero {
constexpr uint32_t kFmMinHz=87500000, kFmMaxHz=108000000;
constexpr std::array<uint32_t,5> kSteps{{10000,50000,100000,200000,1000000}};
enum class Page { receive, entry, menu, gain, audio, receiver, help, error };
enum class Key { left, right, up, down, enter, escape, backspace, menu, frequency, mute, volume_up, volume_down, gain, step, reconnect, digit, point, help, quit };
enum class Connection { disconnected, connecting, ready, error };
struct Snapshot {
  uint32_t requested_hz=96100000,applied_hz=0,requested_rate=2560000,applied_rate=0;
  uint32_t bandwidth_hz=180000, step_hz=100000;
  uint64_t generation=0,known_dropped_samples=0;
  int volume=30, gain_tenth_db=0; bool muted=false,gain_auto=true,audio_ready=false,simulated=false,live_spectrum=false;
  Connection connection=Connection::disconnected;
  Page page=Page::receive;
  std::string error, receiver_name="No receiver",audio_device="default",entry;
  std::vector<int> gains;
  std::array<uint8_t,256> spectrum{};
  std::array<std::array<uint8_t,256>,36> waterfall{};
  int waterfall_rows=0;
};
class ControlModel {
 public:
  Snapshot s;
  bool exit_requested=false,dirty_settings=false;
  int step_index=2,menu_index=0,gain_index=0;
  void restore(uint32_t hz,int volume,bool muted,int step) {
    if(hz>=kFmMinHz&&hz<=kFmMaxHz) s.requested_hz=hz;
    s.volume=std::clamp(volume,0,100);s.muted=muted;
    if(step>=0&&step<int(kSteps.size()))step_index=step;
    s.step_hz=kSteps[step_index];
  }
  void hardware_ready(uint32_t hz,uint32_t rate,std::vector<int> gains,std::string name) {
    s.applied_hz=hz;s.requested_hz=hz;s.applied_rate=rate;s.requested_rate=rate;
    s.gains=std::move(gains);s.receiver_name=std::move(name);s.connection=Connection::ready;
    s.error.clear(); ++s.generation;
  }
  void tune_applied(uint32_t actual) {s.applied_hz=actual;s.requested_hz=actual; ++s.generation;s.error.clear();s.live_spectrum=false;s.waterfall_rows=0;}
  void tune_failed(std::string error) { s.requested_hz=s.applied_hz;s.error=std::move(error);s.page=Page::error; }
  void disconnected(std::string reason) {s.connection=Connection::error;s.audio_ready=false;s.live_spectrum=false;s.error=std::move(reason);++s.generation;}
  void handle(Key key,char digit=0) {
    if(key==Key::quit){exit_requested=true;return;}
    if(s.page==Page::entry) {
      if(key==Key::escape){s.entry.clear();s.page=Page::receive;return;}
      if(key==Key::backspace){if(!s.entry.empty())s.entry.pop_back();return;}
      if(key==Key::digit && s.entry.size()<10 && digit>='0'&&digit<='9'){s.entry.push_back(digit);return;}
      if(key==Key::point && s.entry.find('.')==std::string::npos && s.entry.size()<10){s.entry.push_back('.');return;}
      if(key==Key::enter){
        const auto p=s.entry.find('.');
        if(s.entry.empty())return;
        uint64_t mhz=0, frac=0, mult=100000;
        try {mhz=std::stoull(s.entry.substr(0,p));if(p!=std::string::npos)for(size_t i=p+1;i<s.entry.size();++i){ if(!mult)break;frac+=uint64_t(s.entry[i]-'0')*mult;mult/=10; }}
        catch(...){s.error="INVALID FREQUENCY";return;}
        uint64_t hz=mhz*1000000ULL+frac;
        if(hz<kFmMinHz||hz>kFmMaxHz){s.error="FM: 87.5-108 MHZ";return;}
        s.requested_hz=uint32_t(hz);s.page=Page::receive;s.entry.clear();s.error.clear();dirty_settings=true;
      }
      return;
    }
    if(key==Key::escape){s.page=Page::receive;s.error.clear();return;}
    if(key==Key::mute){s.muted=!s.muted;dirty_settings=true;return;}
    if(key==Key::volume_up||key==Key::volume_down){s.volume=std::clamp(s.volume+(key==Key::volume_up?5:-5),0,100);dirty_settings=true;return;}
    if(key==Key::frequency|| (key==Key::enter&&s.page==Page::receive)) {s.page=Page::entry;s.entry.clear();s.error.clear();return;}
    if(key==Key::help){s.page=Page::help;return;}
    if(key==Key::menu){s.page=Page::menu;menu_index=0;return;}
    if(key==Key::gain){s.page=Page::gain;return;}
    if(key==Key::step){step_index=(step_index+1)%int(kSteps.size());s.step_hz=kSteps[step_index];dirty_settings=true;return;}
    if(s.page==Page::menu) {
      if(key==Key::up)menu_index=(menu_index+4)%5;
      if(key==Key::down)menu_index=(menu_index+1)%5;
      if(key==Key::enter){constexpr Page pages[]={Page::receive,Page::gain,Page::audio,Page::receiver,Page::help};s.page=pages[menu_index];}
      return;
    }
    if(s.page==Page::gain) {
      if(key==Key::enter){s.gain_auto=!s.gain_auto;dirty_settings=true;}
      if(!s.gain_auto&&!s.gains.empty()&&(key==Key::left||key==Key::right)){
        auto it=std::lower_bound(s.gains.begin(),s.gains.end(),s.gain_tenth_db);
        int at=int(it-s.gains.begin());at=std::clamp(at+(key==Key::right?1:-1),0,int(s.gains.size())-1);
        s.gain_tenth_db=s.gains[at];dirty_settings=true;
      }
      return;
    }
    if(s.page==Page::audio||s.page==Page::help||s.page==Page::receiver||s.page==Page::error){
      if(key==Key::reconnect&&s.page==Page::error){s.connection=Connection::connecting;s.error.clear();s.page=Page::receive;}
      return;
    }
    if(s.page==Page::receive&&(key==Key::left||key==Key::right)) {
      const int64_t proposed=int64_t(s.requested_hz)+(key==Key::right?int64_t(s.step_hz):-int64_t(s.step_hz));
      s.requested_hz=uint32_t(std::clamp<int64_t>(proposed,kFmMinHz,kFmMaxHz));dirty_settings=true;
    }
  }
};
}