#include "adsb_module.hpp"
#include "adsb_renderer.hpp"
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
namespace adsb=orc::zero::adsb;
int main(int argc,char** argv) {
  bool demo=false,empty=false,detail=false;std::string path;
  for(int i=1;i<argc;++i) {
    const std::string a=argv[i];
    if(a=="--demo")demo=true;
    else if(a=="--empty")empty=true;
    else if(a=="--detail")detail=true;
    else if(a=="--screenshot"&&i+1<argc)path=argv[++i];
    else {std::cerr<<"usage: orc_zero_adsb_preview --demo [--empty|--detail] --screenshot FILE.ppm\n";return 2;}
  }
  if(!demo||path.empty()){std::cerr<<"preview requires explicit --demo and --screenshot\n";return 2;}
  adsb::Model model;const uint64_t now=1200000000000ull;model.reset(1);
  if(!empty){
    orcsdr::adsb_rx::Frame id{};id.icao=0x4840d6;id.bit_length=112;id.has_callsign=true;
    std::memcpy(id.callsign,"KLM1023",8);id.signal=74;
    model.apply_verified(id,now-3'000'000'000ull,1);
    orcsdr::adsb_rx::Frame pos{};pos.icao=0x4840d6;pos.bit_length=112;
    pos.has_altitude=true;pos.altitude_ft=38000;pos.signal=90;
    model.apply_verified(pos,now-2'000'000'000ull,1);
    orcsdr::adsb_rx::Frame second{};second.icao=0xa29551;second.bit_length=112;
    second.has_callsign=true;std::memcpy(second.callsign,"ASA1310",8);
    second.has_altitude=true;second.altitude_ft=35000;second.signal=55;
    model.apply_verified(second,now-7'000'000'000ull,1);
    orcsdr::adsb_rx::Frame third{};third.icao=0x451dbd;third.bit_length=112;
    third.has_speed=true;third.speed_kts=436;third.has_heading=true;third.heading_deg=271;
    third.signal=62;model.apply_verified(third,now-19'000'000'000ull,1);
  }
  if(detail)model.navigate(adsb::Action::open_detail);
  adsb::Renderer renderer;
  auto snapshot=model.snapshot(now,adsb::Status::demo,true,false);
  snapshot.counters.crc_ok=empty?0:5;
  renderer.render(snapshot);
  if(!renderer.save_ppm(path)){std::cerr<<"failed to save preview: "<<path<<"\n";return 1;}
  std::cout<<"DEMO preview="<<path<<" pixels=320x170 aircraft="<<snapshot.count
           <<" full_clears="<<renderer.stats().full_clears<<"\n";
}
