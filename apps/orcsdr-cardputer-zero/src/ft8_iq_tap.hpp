#pragma once
#include "ft8_audio_tap.hpp"
#include "iq_queue.hpp"
#include "shortwave_ssb.hpp"
#include <algorithm>
#include <array>

namespace orc::zero {
class Ft8IqTap {
 public:
  // Returns 12 kHz USB PCM from one bounded block. A discontinuity discards the
  // current decoder slot; the caller can start again at the next UTC boundary.
  struct Result {size_t frames;bool reset_slot;};
  Result consume(const linux_sdr::IqBlock& block,uint32_t dial,bool direct_q,
                 int16_t* pcm,size_t capacity){
    namespace audio=orcsdr::ftx::audio_tap;
    const auto& meta=block.meta;
    const auto bytes=block.cu8.size();
    const int64_t offset=int64_t(dial)-meta.frequency_hz;
    if(!pcm||capacity==0||!dial||!meta.frequency_hz||!meta.receiver_generation||
       !audio::rate_supported(meta.sample_rate_sps)||!bytes||(bytes&1)||
       bytes>mirror_.size()||offset<-12000||offset>12000){
      generation_=0;
      return {0,true};
    }
    const bool folded=direct_q&&orcsdr::shortwave_ssb::spectrum_inverted(meta.frequency_hz);
    const bool restart=generation_!=meta.receiver_generation||next_!=meta.first_sample_index||
      rate_!=meta.sample_rate_sps||center_!=meta.frequency_hz||dial_!=dial||
      folded_!=folded||meta.discontinuity||meta.dropped_samples_before;
    if(restart){
      if(!audio::begin(&tap_,meta.sample_rate_sps))return {0,true};
      audio::set_dial_offset(&tap_,float(offset));
    }
    if(capacity<audio::max_output_samples(tap_,bytes)){
      generation_=0;
      return {0,true};
    }
    generation_=meta.receiver_generation;
    next_=meta.first_sample_index+bytes/2;
    rate_=meta.sample_rate_sps;
    center_=meta.frequency_hz;
    dial_=dial;
    folded_=folded;
    const uint8_t* input=block.cu8.data();
    if(folded){
      std::copy_n(input,bytes,mirror_.data());
      for(size_t i=1;i<bytes;i+=2)mirror_[i]=uint8_t(255-mirror_[i]);
      input=mirror_.data();
    }
    return {audio::process_cu8(&tap_,input,bytes,pcm,capacity),restart};
  }
 private:
  orcsdr::ftx::audio_tap::Tap tap_{};
  std::array<uint8_t,32768> mirror_{};
  uint64_t generation_=0,next_=0;
  uint32_t rate_=0,center_=0,dial_=0;
  bool folded_=false;
};
}
