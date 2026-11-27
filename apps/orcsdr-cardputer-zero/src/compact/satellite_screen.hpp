#pragma once
#include "p25_screen.hpp"
namespace orc::zero {
struct SatelliteScreen {
  P25Screen tuning;
  CompactRenderer frame;
  Snapshot signal;
  SatelliteScreen(){tuning.requested=137500000;signal.requested_hz=tuning.requested;signal.applied_rate=300000;signal.bandwidth_hz=16000;}
  bool input(Key key,char digit){
    if(!tuning.entry_active&&(key==Key::gain||key==Key::enter||key==Key::up||key==Key::down))return true;
    return tuning.input(key,digit);
  }
  const auto& render(unsigned palette){
    signal.requested_hz=tuning.requested;signal.applied_hz=tuning.applied;signal.palette=palette;
    signal.volume=tuning.volume;signal.muted=tuning.muted;signal.step_hz=tuning.steps[tuning.step_index];
    signal.page=Page::receive;
    frame.render(signal);
    frame.rect(0,0,320,40,frame.panel);frame.badge(1,1,28);
    frame.text(33,2,"OrcSDR SAT RX",frame.green,2);
    char b[64];std::snprintf(b,sizeof(b),"%.4f MHZ NFM  %s",tuning.requested/1e6,tuning.requested==tuning.applied?"RX":"TUNING");
    frame.text(33,21,b,frame.white);
    std::snprintf(b,sizeof(b),"STEP %.1fK VOL %d%% %s",tuning.steps[tuning.step_index]/1000.,tuning.volume,tuning.muted?"MUTED":signal.audio_ready?"AUDIO":"NO AUDIO");
    frame.text(4,32,b,frame.cyan);
    frame.rect(0,160,320,10,frame.panel);frame.keys(4,162,{{"V","FREQ"},{"T","STEP"},{"S/D","VOL"},{"M","MUTE"},{"TAB","DASH"}});
    if(tuning.entry_active){
      frame.rect(10,55,300,55,frame.panel);frame.border(10,55,300,55,frame.cyan);
      frame.text(16,61,"MHZ "+tuning.entry+"_",frame.white,2,23);
      frame.text(16,88,tuning.error.empty()?"ENTER APPLY  ESC CANCEL":tuning.error,frame.yellow);
    }
    return frame.pixels;
  }
};
}
