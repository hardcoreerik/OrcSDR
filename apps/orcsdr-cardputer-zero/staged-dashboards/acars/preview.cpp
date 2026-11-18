// SPDX-License-Identifier: AGPL-3.0-only
#include "acars_dashboard.hpp"
#include "acars_renderer.hpp"
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

using namespace orc::zero::acars_dashboard;
int main(int argc,char** argv){
  bool demo=false,empty=false,detail=false;
  std::string screenshot;
  for(int n=1;n<argc;++n){
    if(std::strcmp(argv[n],"--demo")==0)demo=true;
    else if(std::strcmp(argv[n],"--empty")==0)empty=true;
    else if(std::strcmp(argv[n],"--detail")==0)detail=true;
    else if(std::strcmp(argv[n],"--screenshot")==0&&n+1<argc)screenshot=argv[++n];
    else{std::cerr<<"Usage: orc_zero_acars_preview --demo [--empty] [--detail] --screenshot OUT.ppm\n";return 2;}
  }
  if(!demo||screenshot.empty()){std::cerr<<"Only clearly labeled DEMO preview is supported\n";return 2;}
  Dashboard dash;dash.begin_demo();
  if(!empty){
    constexpr const char* messages[]={
      "1234UAL111 TEST MESSAGE", "REQUEST FUEL STATUS", "ETA REVISED TO 1930Z",
      "POSITION REPORT RECEIVED", "GATE ASSIGNMENT PENDING", "ETB CONTINUATION"};
    for(unsigned n=0;n<6;++n){
      orcsdr::acars::Message m{};
      m.frequency_hz=n%2?131725000:131550000;
      std::memcpy(m.tail,n%2?"N31415.":"N123456",7);
      std::memcpy(m.flight,"UAL111",6);
      m.label[0]='Q';m.label[1]=n%2?'1':'0';
      m.mode='2';m.has_text=true;m.end_marker=n==5?0x17:0x03;
      m.corrected=n==2;m.corrected_bits=m.corrected?1:0;
      m.raw_bytes=uint16_t(16+std::strlen(messages[n]));
      m.text_length=uint16_t(std::strlen(messages[n]));
      std::memcpy(m.text,messages[n],m.text_length);
      dash.append_demo(m, uint64_t(n+1)*1000000000ULL);
    }
  }
  if(detail)dash.input(Input::detail);
  Renderer renderer;renderer.render(dash.snapshot(),7000000000ULL);
  if(!renderer.save_ppm(screenshot)){std::cerr<<"Unable to write PPM: "<<screenshot<<"\n";return 1;}
  std::cout<<"DEMO 320x170 preview: "<<screenshot<<" (messages="<<dash.snapshot().held<<")\n";
}
