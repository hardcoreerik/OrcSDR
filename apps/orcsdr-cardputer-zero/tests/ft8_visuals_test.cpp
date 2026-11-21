#include "ft8_visuals.hpp"
#include <cassert>
#include <iostream>
int main(){
  static_assert(orc::zero::Ft8WorldMap::top+orc::zero::Ft8WorldMap::height==138);
  static_assert(orc::zero::Ft8WorldMap::width==2*orc::zero::Ft8WorldMap::height);
  static_assert(orc::zero::Ft8Visuals::height>=68);
  orc::zero::Ft8Visuals visual;std::array<int16_t,3840> audio;
  for(size_t n=0;n<audio.size();++n)audio[n]=int16_t(12000*std::sin(6.283185307179586*1000*n/12000));
  visual.consume(audio.data(),audio.size(),true);assert(visual.rows()==1);
  const auto& row=visual.newest();assert(row[106]>200&&row[20]<100);
  orc::zero::CompactRenderer frame;visual.draw(frame,0);auto pixels=frame.pixels;
  visual.consume(audio.data(),audio.size(),false);visual.draw(frame,1);
  assert(frame.pixels!=pixels&&frame.full_clears==0);
  orc::zero::Ft8WorldMap map;map.draw(frame);pixels=frame.pixels;
  orcsdr::ft8::Decode d;std::snprintf(d.grid,sizeof(d.grid),"FN42");std::snprintf(d.callsign,sizeof(d.callsign),"K1ABC");std::snprintf(d.message,sizeof(d.message),"CQ K1ABC FN42");map.add(d);
  map.draw(frame);assert(map.located()==1&&frame.pixels!=pixels&&frame.full_clears==0);
  orcsdr::ft8::GeoPoint p;assert(orcsdr::ft8::maidenhead_center("FN42",&p));int x,y;
  assert(orcsdr::ft8::world_map::project(p.latitude,p.longitude,map.width,map.height,&x,&y));
  assert(frame.pixels[(map.top+y)*320+x]==frame.yellow);
  assert(map.heard_count()==1);
  assert(frame.pixels[139*320+5]==frame.white);
  const auto with_contact=frame.pixels;
  std::snprintf(d.callsign,sizeof(d.callsign),"K2XYZ");std::snprintf(d.message,sizeof(d.message),"K1ABC K2XYZ RR73");
  d.grid[0]='?';map.add(d);map.draw(frame);assert(map.located()==1&&frame.pixels!=with_contact&&frame.full_clears==0);
  for(int j=0;j<100;++j)map.add(d);
  assert(map.heard_count()==2);
  for(int j=0;j<5;++j){std::snprintf(d.callsign,sizeof(d.callsign),"N%dTEST",j);map.add(d);}
  assert(map.heard_count()==5&&std::string(map.heard(4).callsign)=="N0TEST");
  visual.clear();assert(visual.rows()==0);
  std::cout<<"FT8 live-audio frequency mapping, palette, grid markers, bounded redraw PASS; visual bytes "<<sizeof(visual)<<" map bytes "<<sizeof(map)<<"\n";
}
