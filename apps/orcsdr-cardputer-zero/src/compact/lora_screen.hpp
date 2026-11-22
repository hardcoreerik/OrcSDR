#pragma once
#include "p25_screen.hpp"
#include <cstring>
#include "lora_native_decoder.hpp"
#include "lora_preamble_detector.hpp"
#include "lora_region_plan.hpp"
namespace orc::zero {
struct LoraScreen {
  P25Screen tuning;
  CompactRenderer frame;
  uint8_t sf=11;
  size_t region=0;uint16_t slot=20;
  bool layout=false,capturing=false,decoding=false,manual=false;
  uint32_t captures=0,crc_ok=0,crc_failed=0,decode_ms=0;
  uint64_t revision=0,painted=~uint64_t{0};
  size_t offset=0;
  orcsdr::lora_preamble::Status preamble{};
  std::vector<std::string> lines;
  LoraScreen(){set_plan(0,orcsdr::lora_plan::default_slot(0));tuning.step_index=1;}
  void set_plan(size_t requested_region,uint16_t requested_slot){
    region=std::min(requested_region,orcsdr::lora_plan::kRegions.size()-1);
    slot=std::clamp<uint16_t>(requested_slot,1,orcsdr::lora_plan::slot_count(region));
    tuning.requested=orcsdr::lora_plan::frequency_hz(region,slot);++revision;
  }
  bool input(Key key,char digit){
    if(key==Key::escape&&!tuning.entry_active)return false;
    if(tuning.entry_active||key==Key::frequency||key==Key::escape)tuning.input(key,digit);
    else if(key==Key::step)set_plan((region+1)%orcsdr::lora_plan::kRegions.size(),orcsdr::lora_plan::default_slot((region+1)%orcsdr::lora_plan::kRegions.size()));
    else if(key==Key::left||key==Key::right)set_plan(region,key==Key::left?(slot==1?orcsdr::lora_plan::slot_count(region):slot-1):(slot==orcsdr::lora_plan::slot_count(region)?1:slot+1));
    else if(key==Key::gain)sf=sf==12?7:sf+1;
    else if(key==Key::enter)manual=true;
    else if(key==Key::up&&offset)--offset;
    else if(key==Key::down&&offset+3<lines.size())++offset;
    ++revision;return true;
  }
  void add(const orcsdr::lora_native::Packet& packet){
    char b[64];std::snprintf(b,sizeof(b),"%08X > %08X PORT %u",packet.sender,packet.destination,packet.port);
    std::string text(packet.text,strnlen(packet.text,sizeof(packet.text)));
    for(char& c:text)if(c<32||c>126)c='?';
    lines.insert(lines.begin(),text.empty()?(packet.encrypted?"ENCRYPTED PACKET":"NO TEXT"):text);
    lines.insert(lines.begin(),b);
    if(lines.size()>32)lines.resize(32);
    offset=0;++revision;
  }
  const auto& render(){
    if(!layout){
      frame.fill(frame.bg);frame.badge(1,1,28);frame.text(33,3,"OrcSDR LORA/MESH",frame.green,2,23);
      frame.keys(4,160,{{"F","FREQ"},{"G","SF"},{"ENTER","CAPTURE"},{"SPACE","DASH"}});layout=true;
    }
    if(painted==revision)return frame.pixels;
    frame.rect(33,21,287,13,frame.bg);char b[80];
    std::snprintf(b,sizeof(b),"%s %u/%u %.3f SF%u %s",orcsdr::lora_plan::kRegions[region].code,slot,orcsdr::lora_plan::slot_count(region),tuning.requested/1e6,sf,tuning.requested==tuning.applied?"RX":"TUNE");
    frame.text(33,23,b,frame.white,1,47);
    frame.rect(0,36,320,120,frame.bg);
    if(tuning.entry_active){
      frame.text(4,42,"ENTER MHZ: "+tuning.entry+"_",frame.white,2,25);
      frame.text(4,66,tuning.error.empty()?"ENTER APPLY  ESC CANCEL":tuning.error,frame.yellow);
    }else{
      std::snprintf(b,sizeof(b),"%s  WINDOWS %u FIRES %u",decoding?"DECODING":capturing?"CAPTURING":"LISTENING",preamble.windows,preamble.fires);
      frame.text(4,39,b,frame.cyan,1,52);
      std::snprintf(b,sizeof(b),"CAP %u CRC OK %u FAIL %u %uMS",captures,crc_ok,crc_failed,decode_ms);
      frame.text(4,51,b,frame.white,1,52);
      if(lines.empty())frame.text(4,76,"WAITING FOR RECEIVED PACKETS",frame.muted);
      for(size_t n=offset;n<std::min(lines.size(),offset+6);++n)frame.text(4,68+int(n-offset)*11,lines[n],n%2?frame.white:frame.green,1,52);
      frame.text(4,141,"LEFT/RIGHT SLOT  T REGION  UP/DOWN LOG",frame.muted);
    }
    painted=revision;return frame.pixels;
  }
};
}
