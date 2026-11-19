#pragma once
#include "compact_renderer.hpp"
#include "p25_decoder_core.hpp"
#include "ft8_tuning.hpp"
namespace orc::zero {
struct P25Screen {
  CompactRenderer frame;
  uint32_t requested=851000000,applied=0;
  int volume=35;
  bool muted=false,audio_ready=false,entry_active=false,layout=false;
  unsigned step_index=0;
  orcsdr::p25core::Modulation modulation=orcsdr::p25core::Modulation::auto_detect;
  orcsdr::p25core::Snapshot state{};
  uint64_t revision=0,painted=~uint64_t{0};
  uint32_t voice_pcm=0,blocked_voice=0;
  std::string entry,error;
  inline static constexpr uint32_t steps[]={12500,100000,1000000,5000000};
  bool input(Key key,char digit){
    if(entry_active){
      if(key==Key::escape){entry_active=false;error.clear();}
      else if(key==Key::enter){
        uint32_t hz=0;
        if(orcsdr::ft8::tuning::parse_mhz(entry.c_str(),&hz)&&hz>=24000000&&hz<=1766000000){
          requested=hz;entry_active=false;error.clear();
        }else error="USE 24 TO 1766 MHZ";
      }else if(key==Key::backspace){if(!entry.empty())entry.pop_back();}
      else if(key==Key::digit||key==Key::point){
        char b[20]{};std::snprintf(b,sizeof(b),"%s",entry.c_str());
        orcsdr::ft8::tuning::entry_key(b,sizeof(b),key==Key::point?'.':digit);entry=b;
      }
    }else{
      if(key==Key::escape)return false;
      if(key==Key::frequency){entry.clear();error.clear();entry_active=true;}
      if(key==Key::left||key==Key::right)
        requested=uint32_t(std::clamp<int64_t>(int64_t(requested)+(key==Key::left?-int64_t(steps[step_index]):steps[step_index]),24000000,1766000000));
      if(key==Key::step)step_index=(step_index+1)%4;
      if(key==Key::gain)modulation=static_cast<orcsdr::p25core::Modulation>((unsigned(modulation)+1)%3);
      if(key==Key::volume_up)volume=std::min(100,volume+5);
      if(key==Key::volume_down)volume=std::max(0,volume-5);
      if(key==Key::mute)muted=!muted;
    }
    ++revision;return true;
  }
  const auto& render(){
    if(!layout){
      frame.fill(CompactRenderer::bg);
      frame.text(4,3,"OrcSDR P25 RX",CompactRenderer::green,2);
      
      frame.text(4,160,"F FREQ T STEP G MODE S/D VOL SPACE DASH",CompactRenderer::cyan);
      layout=true;
    }
    if(painted==revision)return frame.pixels;
    frame.rect(0,37,320,118,CompactRenderer::bg);
    char b[80];
    std::snprintf(b,sizeof(b),"%.4f MHZ %s",requested/1e6,requested==applied?"RX":"TUNING");
    frame.text(4,39,b,CompactRenderer::white);
    std::snprintf(b,sizeof(b),"STEP %.1fK  %s",steps[step_index]/1000.,orcsdr::p25core::modulation_name(modulation));
    frame.text(4,51,b,CompactRenderer::cyan);
    if(entry_active){
      frame.text(4,72,"ENTER MHZ: "+entry+"_",CompactRenderer::yellow,2,25);
      frame.text(4,98,error.empty()?"ENTER APPLY  ESC CANCEL":error,CompactRenderer::cyan);
    }else{
      std::snprintf(b,sizeof(b),"%s NAC %03X NID %u TSBK %u",state.frame_sync?"LOCK":"SEARCH",state.nac,state.nid_good,state.tsbk_good);
      frame.text(4,68,b,state.frame_sync?CompactRenderer::green:CompactRenderer::yellow,1,52);
      std::snprintf(b,sizeof(b),"SYS %03X WACN %05X SITE %u/%u",state.system_id,state.wacn,state.rfss,state.site);
      frame.text(4,81,b,CompactRenderer::white,1,52);
      const auto& g=state.current_grant;
      std::snprintf(b,sizeof(b),"TG %u SRC %u %.4fM",g.talkgroup,g.source_id,g.frequency_hz/1e6);
      frame.text(4,94,g.valid?b:"NO DECODED GRANT",CompactRenderer::cyan,1,52);
      std::snprintf(b,sizeof(b),"VOL %d%% %s PCM %u",volume,muted?"MUTED":audio_ready?"AUDIO":"NO AUDIO",voice_pcm);
      frame.text(4,109,b,CompactRenderer::white,1,52);
      frame.text(4,124,"PHASE I CLEAR VOICE / MANUAL TUNE",CompactRenderer::cyan);
      frame.text(4,138,"NO TRUNK FOLLOW OR PHASE II VOICE",CompactRenderer::muted);
    }
    painted=revision;return frame.pixels;
  }
};
}
