#include "map_view.hpp"
#include <M5GFX.h>
#include <cassert>
#include <cstdio>
#include <cstring>

namespace orcsdr::maps {
Frame next_frame=Frame::pending;
bool preview=true;
int service_calls=0;
orcmap::Viewport last_camera;
bool last_labels=true;
bool regional_view(int32_t lat,int32_t lon,double,int width,int height,orcmap::Viewport* out) {
  out->center_lat_deg=lat/1e7;out->center_lon_deg=lon/1e7;
  out->width_px=width;out->height_px=height;out->zoom=8;return true;
}
bool project(const orcmap::Viewport&,int32_t,int32_t,int* x,int* y) {*x=5;*y=6;return true;}
Frame draw(const orcmap::Viewport& camera,lgfx::LovyanGFX& target,int,int,FrameInfo* info,const char*,bool labels) {
  ++service_calls;
  last_camera=camera;last_labels=labels;
  if(preview||next_frame==Frame::ready) {
    target.drawString("OrcMaps pixels",0,0);
    std::snprintf(info->credit,sizeof(info->credit),"Original pack attribution");
  }
  return next_frame;
}
}
int main() {
  using namespace orcsdr::maps;
  lgfx::LovyanGFX display;
  for(const auto state:{Frame::pending,Frame::ready,Frame::unavailable}) {
    next_frame=state;display.strings.clear();const auto clears=display.region_clears;
    // A stale saved legacy identity must never bypass the shared engine.
    const auto view=draw_region(display,444013775,-1229940563,100,10,20,622,302,
                                0,1,2,"lane_county_map",true);
    assert(view.valid&&view.frame.state==state);
    assert(display.region_clears==clears); // Opaque frame/preview replaces pixels without blanking first.
    int x=0,y=0;assert(project(view,444013775,-1229940563,&x,&y));
    assert(x==15&&y==26);
    if(state==Frame::pending) {
      assert(std::strcmp(view.frame.credit,"Original pack attribution")==0);
      assert(std::strcmp(view.frame.status,"UPDATING LOCAL MAP")==0);
    }
  }
  assert(service_calls==3);
  next_frame=Frame::pending;preview=false;
  const auto empty_clears=display.region_clears;
  const auto loading=draw_region(display,0,0,25,10,20,622,302,0,1,2);
  assert(display.region_clears==empty_clears);
  assert(std::strcmp(loading.frame.status,"LOADING LOCAL MAP")==0);
  next_frame=Frame::unavailable;
  draw_region(display,0,0,25,10,20,622,302,0,1,2);
  assert(display.region_clears==empty_clears+1);
  next_frame=Frame::pending;
  orcmap::Viewport camera;camera.width_px=842;camera.height_px=330;
  camera.center_lat_deg=10;camera.center_lon_deg=-179.9;camera.zoom=12;camera.tile_size_px=310;
  const auto moved=draw_camera(display,camera,50,182,0,1,2,nullptr,false);
  assert(moved.valid&&!last_labels&&last_camera.zoom==12&&last_camera.tile_size_px==310);
  assert(last_camera.center_lon_deg==camera.center_lon_deg&&last_camera.center_lat_deg==10);
  assert(moved.camera.center_lon_deg==last_camera.center_lon_deg&&moved.x==50&&moved.y==182);
  const auto readable=draw_camera(display,camera,50,182,0,1,2,nullptr,true,2);
  assert(readable.valid&&display.text_size==2);
  preview=true;camera.width_px=160;camera.height_px=200;display.strings.clear();
  draw_camera(display,camera,50,182,0,1,2,nullptr,true,2);
  std::string caption;
  for(const auto& line:display.strings)if(line!="OrcMaps pixels")caption+=line;
  assert(caption=="Original pack attribution"&&display.text_size==2);
  const auto calls=service_calls;camera.width_px=0;
  assert(!draw_camera(display,camera,50,182,0,1,2).valid&&service_calls==calls);
  std::puts("MAP_VIEW_PASS pending/ready/unavailable use one engine and projection; pending credit retained");
}
