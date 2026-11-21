#pragma once
#include "p25_decoder_core.hpp"
namespace orc::zero {
// Phase I subset of main.cpp's service_p25_follow: same freshness/acquire/hang limits.
class P25Follow {
 public:
  bool voice=false;
  uint16_t held=0;
  orcsdr::p25core::Grant grant{};
  uint32_t update(uint32_t control,bool enabled,bool hold,const orcsdr::p25core::Snapshot& state,uint32_t now){
    if(control_!=control){control_=control;voice=false;held=0;grant={};skipped_=0;return control;}
    if(!hold)held=0;
    if(!enabled){voice=false;grant={};return control;}
    if(voice){
      const bool encrypted=state.voice_encryption.valid&&state.voice_encryption.encrypted;
      const bool acquire_timeout=!state.last_voice_ms&&uint32_t(now-started_)>=3000;
      const bool hang=state.last_voice_ms&&uint32_t(now-state.last_voice_ms)>=1800;
      if(encrypted||acquire_timeout||hang){
        if(encrypted){skipped_=grant.talkgroup;skip_started_=now;}
        voice=false;return control;
      }
      return grant.frequency_hz;
    }
    const auto& next=state.current_grant;
    if(!next.valid||!next.talkgroup||next.frequency_hz<24000000||next.frequency_hz>1766000000||
       next.frequency_hz==control||next.tdma||next.encrypted||uint32_t(now-next.seen_ms)>1500)return control;
    if(skipped_&&uint32_t(now-skip_started_)>=30000)skipped_=0;
    if(next.talkgroup==skipped_)return control;
    if(hold){if(!held)held=next.talkgroup;if(held!=next.talkgroup)return control;}
    grant=next;voice=true;started_=now;return grant.frequency_hz;
  }
 private:
  uint32_t control_=0,started_=0,skip_started_=0;
  uint16_t skipped_=0;
};
}
