#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace orc::zero {
// Assemble one FT8 slot from continuous 12 kHz PCM. The receiver supplies
// UTC for the first sample; later blocks follow the sample clock.
class Ft8Slots {
 public:
  static constexpr uint64_t kRate=12000;
  static constexpr uint64_t kDuration=15*kRate;
  static constexpr uint64_t kClockTolerance=1200;

  template<class Emit>
  void offer(const int16_t* pcm,size_t count,uint64_t first_utc_ns,bool reset,Emit&& emit){
    if(!pcm||!count||count>512||!first_utc_ns){clear();return;}
    const uint64_t first=(first_utc_ns/1000000000)*kRate+
      (first_utc_ns%1000000000)*kRate/1000000000;
    if(reset||!next_|| (first>next_?first-next_:next_-first)>kClockTolerance){
      clear();
      next_=first;
    }
    while(count){
      const auto phase=next_%kDuration;
      if(!phase){
        samples_.clear();
        epoch_ms_=(next_/kDuration)*15000;
      }
      const size_t n=std::min<uint64_t>(count,kDuration-phase);
      if(samples_.size()==phase)samples_.insert(samples_.end(),pcm,pcm+n);
      pcm+=n;
      count-=n;
      next_+=n;
      if(samples_.size()==kDuration){
        emit(epoch_ms_,samples_.data(),samples_.size());
        samples_.clear();
      }
    }
  }

  void clear(){next_=0;samples_.clear();}
 private:
  uint64_t next_=0,epoch_ms_=0;
  std::vector<int16_t> samples_;
};
}
