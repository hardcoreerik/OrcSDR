#include "weather_noaa.hpp"
#include <cstdint>

namespace orcsdr::weather::noaa {
namespace {
constexpr uint32_t kChannels[kChannelCount] = {
  162400000u, 162425000u, 162450000u, 162475000u,
  162500000u, 162525000u, 162550000u
};
}
uint32_t channel_hz(size_t index) { return index < kChannelCount ? kChannels[index] : kChannels[0]; }
int channel_index(uint32_t hz) {
  for (size_t i=0;i<kChannelCount;++i) if (kChannels[i] == hz) return static_cast<int>(i);
  return -1;
}
uint32_t nearest_channel_hz(uint32_t hz) {
  size_t best=0; uint32_t best_delta=UINT32_MAX;
  for(size_t i=0;i<kChannelCount;++i){
    const uint32_t d = kChannels[i] > hz ? kChannels[i]-hz : hz-kChannels[i];
    if(d < best_delta){best=i;best_delta=d;}
  }
  return kChannels[best];
}
uint32_t next_channel_hz(uint32_t hz) {
  const int i=channel_index(hz); return kChannels[i<0?0:(static_cast<size_t>(i)+1)%kChannelCount];
}
uint32_t previous_channel_hz(uint32_t hz) {
  const int i=channel_index(hz); return kChannels[i<0?0:(static_cast<size_t>(i)+kChannelCount-1)%kChannelCount];
}
void ScanPlan::start(){ index_=0; samples_=0; strongest_=-1; strongest_level_=-1000.0f; active_=true; complete_=false; }
void ScanPlan::cancel(){ active_=false; complete_=false; }
bool ScanPlan::active() const { return active_; }
bool ScanPlan::complete() const { return complete_; }
uint32_t ScanPlan::current_frequency_hz() const { return kChannels[index_ < kChannelCount ? index_ : 0]; }
void ScanPlan::offer(float level){ if(!active_ || index_>=kChannelCount) return; ++samples_; if(strongest_<0 || level>strongest_level_){ strongest_=static_cast<int>(index_); strongest_level_=level; } }
void ScanPlan::advance(){ if(!active_) return; ++index_; if(index_>=kChannelCount){ active_=false; complete_=true; } }
size_t ScanPlan::sample_count() const { return samples_; }
int ScanPlan::strongest_index() const { return strongest_; }
uint32_t ScanPlan::strongest_frequency_hz() const { return strongest_>=0 ? kChannels[static_cast<size_t>(strongest_)] : 0; }
}  // namespace orcsdr::weather::noaa
