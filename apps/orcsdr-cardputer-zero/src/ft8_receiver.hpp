#pragma once
#include "ft8_iq_tap.hpp"
#include "ft8_native_backend.hpp"
#include "ft8_slots.hpp"

namespace orc::zero {
// Owned by the receive/decoder worker. The callback borrows the result
// buffer only until it returns.
class Ft8Receiver {
 public:
  using AudioObserver=void(*)(const int16_t*,size_t,bool,void*);
  void observe_audio(AudioObserver observer,void* context){observer_=observer;context_=context;}
  bool begin(){
    slots_.clear();
    return decoder_.begin(12000,orcsdr::ftx::Mode::ft8,{});
  }
  template<class Emit>
  bool consume(const linux_sdr::IqBlock& block,uint32_t dial,bool direct_q,
               uint64_t utc_ns,Emit&& emit){
    if(!decoder_.ready()||!utc_ns){slots_.clear();return false;}
    const auto result=tap_.consume(block,dial,direct_q,pcm_.data(),pcm_.size());
    if(!result.frames){
      if(result.reset_slot)slots_.clear();
      return false;
    }
    if(observer_)observer_(pcm_.data(),result.frames,result.reset_slot,context_);
    slots_.offer(pcm_.data(),result.frames,utc_ns,result.reset_slot,
      [&](uint64_t epoch,const int16_t* pcm,size_t frames){
        if(!decoder_.begin_slot(epoch)||!decoder_.offer_audio(pcm,frames))return;
        const auto count=decoder_.finish_slot(decoded_.data(),decoded_.size());
        emit(epoch,decoded_.data(),count);
      });
    return true;
  }
 private:
  AudioObserver observer_=nullptr;void* context_=nullptr;
  Ft8IqTap tap_;
  Ft8Slots slots_;
  orcsdr::ftx::native::Backend decoder_;
  std::array<int16_t,512> pcm_{};
  std::array<orcsdr::ft8::Decode,16> decoded_{};
};
}
