#include "sstv_auto_detect.hpp"
#include <algorithm>
#include <cmath>

namespace orcsdr::sstv::auto_detect {
namespace {
constexpr uint32_t kRate=12000;
constexpr unsigned kHistory=8;
struct Pulse {uint32_t begin=0; uint16_t length=0;};
Pulse g_pulses[kHistory]{};
unsigned g_count=0;
constexpr Mode kModes[] = {
  Mode::robot36,Mode::robot72,Mode::martin_m1,Mode::martin_m2,
  Mode::scottie_s1,Mode::scottie_s2,Mode::scottie_dx,
  Mode::pd50,Mode::pd90,Mode::pd120,Mode::pd160,Mode::pd180,
  Mode::pd240,Mode::pd290
};
}
void reset() {
  g_count=0;
}
unsigned pending_pulses() {return g_count;}
Candidate observe(uint32_t start,uint16_t length) {
  Candidate out{};
  // Ignore very short/long noise bursts; real supported syncs are 4.862-20ms.
  if(length<35 || length>350) return out;
  if(g_count && (start-g_pulses[g_count-1].begin)>kRate*3u)g_count=0;
  if(g_count==kHistory) {
    for(unsigned i=1;i<kHistory;++i)g_pulses[i-1]=g_pulses[i];
    --g_count;
  }
  g_pulses[g_count++]={start,length};
  if(g_count<6)return out;

  float best_quality=-1.f,second_quality=-1.f;
  Mode best_mode=Mode::auto_vis;
  for(Mode mode:kModes) {
    const ModeSpec* m=mode_spec(mode);
    if(!m)continue;
    const float period=static_cast<float>(m->line_us)*kRate/1000000.f;
    const float width=static_cast<float>(m->sync_us)*kRate/1000000.f;
    float period_err=0, width_err=0;
    bool valid=true;
    for(unsigned i=1;i<g_count;++i) {
      const float measured=static_cast<float>(g_pulses[i].begin-g_pulses[i-1].begin);
      const float error=std::fabs(measured-period);
      // Treat >=1 ms absolute drift as a potential clock discrepancy,
      // and allow 2.5% for oscillator error and noisy edge extraction.
      if(error>std::max(12.f,period*.025f)) { valid=false;break; }
      period_err+=error/period;
    }
    if(!valid)continue;
    for(unsigned i=0;i<g_count;++i) {
      const float error=std::fabs(static_cast<float>(g_pulses[i].length)-width);
      if(error>std::max(18.f,width*.3f)) { valid=false;break; }
      width_err+=error/width;
    }
    if(!valid)continue;
    period_err/=static_cast<float>(g_count-1);
    width_err/=static_cast<float>(g_count);
    const float quality=std::clamp(100.f-900.f*period_err-65.f*width_err,0.f,100.f);
    if(quality>best_quality) {
      second_quality=best_quality;
      best_quality=quality;
      best_mode=mode;
    } else if(quality>second_quality) second_quality=quality;
  }
  // Require a clean sequence and a substantial best-vs-runner-up margin.
  // Mode family/offset validation is subsequently performed by line tracking.
  if(best_quality>=75.f && best_quality-second_quality>=12.f) {
    out.mode=best_mode;
    out.score=static_cast<uint8_t>(best_quality+.5f);
    out.pulses=static_cast<uint8_t>(g_count);
    out.sync_start=start;
    out.locked=true;
  }
  return out;
}
} // namespace orcsdr::sstv::auto_detect
