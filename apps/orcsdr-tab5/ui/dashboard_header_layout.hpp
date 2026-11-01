#pragma once
#include <algorithm>

namespace orcsdr::audio_header {
struct HeaderRect { int x=0,y=0,w=0,h=0; };
struct HeaderLayout {
  HeaderRect viewport,brand,badge,battery,status,home,mute,visualizer,settings,tray;
  int brand_x=128,brand_y=38,subtitle_y=82,brand_size=5;
  int slider_x=924,slider_y=113,slider_w=232,content_top=112;
  bool legacy=true,valid=false;
};
inline bool header_inside(HeaderRect outer,HeaderRect r) {
  return r.w>0 && r.h>0 && r.x>=outer.x && r.y>=outer.y &&
      r.x-outer.x<=outer.w-r.w && r.y-outer.y<=outer.h-r.h;
}
inline bool header_overlap(HeaderRect a,HeaderRect b) {
  return a.x<b.x+b.w && b.x<a.x+a.w && a.y<b.y+b.h && b.y<a.y+a.h;
}
inline HeaderLayout make_header_layout(HeaderRect v) {
  HeaderLayout l{};l.viewport=v;
  if (v.x==0 && v.y==0 && v.w==1280 && v.h==720) {
    l.brand={20,12,350,92};l.badge={24,14,88,88};l.battery={1158,12,54,54};
    l.status={420,12,446,54};l.home={1040,8,58,58};l.mute={1099,12,54,54};
    l.visualizer={};l.settings={1217,12,51,54};l.tray={900,72,280,68};
    l.valid=true;return l;
  }
  l.legacy=false;
  if (v.w<360 || v.h<224) return l;
  constexpr int button=44,gap=4,row=56,nav_w=4*button+3*gap;
  const bool wide=v.w>=800;
  const int nav_x=v.x+v.w-nav_w;
  l.home={nav_x,v.y,button,button};l.mute={nav_x+button+gap,v.y,button,button};
  l.visualizer={};
  l.battery={nav_x+2*(button+gap),v.y,button,row};
  l.settings={nav_x+3*(button+gap),v.y,button,button};
  l.brand={v.x,v.y,wide?224:nav_x-v.x-8,row};
  l.badge={v.x,v.y+4,44,44};l.brand_x=v.x+52;l.brand_y=v.y+16;l.subtitle_y=v.y+43;
  l.brand_size=l.brand.w-52>=144?4:3;
  if (wide) {
    l.status={l.brand.x+l.brand.w+8,v.y,nav_x-8-(l.brand.x+l.brand.w+8),row};
    l.content_top=v.y+row+8;
  } else {
    l.status={v.x,v.y+row+8,v.w,row};
    l.content_top=v.y+2*row+16;
  }
  l.tray={std::clamp(l.mute.x-80,v.x,v.x+v.w-280),l.mute.y+l.mute.h+8,280,68};
  l.slider_x=l.tray.x+24;l.slider_y=l.tray.y+41;l.slider_w=l.tray.w-48;
  l.valid=header_inside(v,l.brand) && header_inside(v,l.battery) && header_inside(v,l.status) &&
      header_inside(v,l.home) && header_inside(v,l.mute) &&
      header_inside(v,l.settings) && header_inside(v,l.tray) && !header_overlap(l.brand,l.home) &&
      !header_overlap(l.status,l.battery) && l.content_top<v.y+v.h;
  return l;
}
} // namespace orcsdr::audio_header
