#pragma once

#include "dashboard_header_layout.hpp"

#include <algorithm>
#include <array>

namespace orcsdr::adsb::layout {

struct Rect { int x = 0, y = 0, w = 0, h = 0; };
struct View {
  audio_header::HeaderRect viewport{};
  Rect content{};
  std::array<Rect, 5> tabs{};
  Rect radar_status{}, radar_map{}, radar_map_area{}, radar_stats{}, radar_list{};
  Rect list_rows{}, list_summary{};
  std::array<Rect, 6> list_items{};
  Rect list_lock{},target_lock{},target_map_button{},target_atc_button{},settings_open_data{};
  Rect target_identity{}, target_metrics{}, target_signal{};
  std::array<Rect, 3> stats_charts{};
  std::array<Rect, 5> stats_metrics{};
  std::array<Rect, 4> stats_data{};
  Rect settings_main{};
  std::array<Rect, 4> settings_data{};
  Rect latitude{}, longitude{}, range{}, gain{}, exit{};
  Rect map_zoom_in{}, map_zoom_out{}, map_labels{};
  Rect keypad{}, keypad_value{}, keypad_cancel{}, keypad_save{};
  std::array<Rect, 13> keypad_keys{};
  bool legacy = true, portrait = false, valid = false;
};

constexpr bool inside(Rect a, Rect b) {
  return b.w > 0 && b.h > 0 && b.x >= a.x && b.y >= a.y &&
         b.x - a.x <= a.w - b.w && b.y - a.y <= a.h - b.h;
}
constexpr bool contains(audio_header::HeaderRect a, Rect b) {
  return inside({a.x, a.y, a.w, a.h}, b);
}
constexpr Rect make_rect(int x, int y, int w, int h) { return {x,y,w,h}; }
constexpr const char* tab_label(size_t index,int width) {
  constexpr const char* full[]={"RADAR","LIST","TARGET","STATS","SETTINGS"};
  constexpr const char* compact[]={"RADAR","LIST","TARGET","STATS","SETUP"};
  if(index>=5)return "";
  return width<100?compact[index]:full[index];
}
constexpr int tab_text_width(const char* s) {
  int n=0;if(s)while(s[n])++n;return n*10;
}

constexpr View make_view(audio_header::HeaderRect safe) {
  View v{}; v.viewport = safe;
  if (safe.x == 0 && safe.y == 0 && safe.w == 1280 && safe.h == 720) {
    v.content = {0, 100, 1280, 546};
    for (int i=0;i<5;++i) v.tabs[i] = {i*256,646,256,74};
    v.radar_status={14,88,210,536}; v.radar_map={234,88,646,390};
    v.radar_map_area={246,164,622,302};
    v.radar_stats={234,488,646,136}; v.radar_list={890,88,376,536};
    v.list_rows={14,88,842,536}; v.list_summary={874,88,392,536};
    v.list_lock={1144,108,96,42};v.target_lock={300,104,110,44};
    v.target_map_button={1005,550,210,54};v.target_atc_button={230,462,250,54};
    v.settings_open_data={496,108,752,496};
    for(int i=0;i<6;++i)v.list_items[i]={26,143+i*76,818,68};
    v.target_identity={14,88,500,536}; v.target_metrics={532,88,706,536};
    v.target_signal={532,532,706,92};
    v.stats_charts={{{14,88,400,226},{426,88,400,226},{838,88,428,226}}};
    for(int i=0;i<5;++i)v.stats_metrics[i]={14+i*250,326,238,120};
    for(int i=0;i<4;++i)v.stats_data[i]={14+i*313,458,301,166};
    v.settings_main={14,88,1252,536};
    v.latitude={225,158,225,52};v.longitude={225,240,225,52};
    v.range={225,322,225,52};v.gain={38,430,112,48};v.exit={38,538,412,58};
    v.map_zoom_in={812,176,48,48};v.map_zoom_out={812,232,48,48};v.map_labels={26,252,186,48};
    v.keypad={690,102,570,510};v.keypad_value={730,170,490,55};
    v.keypad_cancel={730,508,230,58};v.keypad_save={990,508,230,58};
    for(int i=0;i<13;++i)v.keypad_keys[i]={730+(i%4)*122,245+(i/4)*64,112,54};
    v.valid=true; return v;
  }
  v.legacy=false; v.portrait=safe.h>safe.w;
  const auto h=audio_header::make_header_layout(safe);
  if(!h.valid)return v;
  const int margin=v.portrait?8:12, gap=8, footer=v.portrait?104:56;
  const int footer_y=safe.y+safe.h-margin-footer;
  const int tab_gap=6, tab_w=(safe.w-2*margin-4*tab_gap)/5;
  for(int i=0;i<5;++i)v.tabs[i]={safe.x+margin+i*(tab_w+tab_gap),footer_y,tab_w,48};
  v.content={safe.x+margin,h.content_top,safe.w-2*margin,footer_y-gap-h.content_top};
  if(v.content.w<=0||v.content.h<=0)return v;
  if(v.portrait){
    const int row=52, map_h=std::max(240,v.content.h*48/100);
    v.radar_status={v.content.x,v.content.y,v.content.w,row};
    v.radar_map={v.content.x,v.content.y+row+gap,v.content.w,map_h};
    v.radar_map_area={v.radar_map.x+8,v.radar_map.y+64,v.radar_map.w-16,v.radar_map.h-76};
    const int rest_y=v.radar_map.y+map_h+gap, rest_h=v.content.y+v.content.h-rest_y;
    v.radar_stats={v.content.x,rest_y,v.content.w,std::max(70,rest_h/2)};
    v.radar_list={v.content.x,v.radar_stats.y+v.radar_stats.h+gap,v.content.w,
                  v.content.y+v.content.h-(v.radar_stats.y+v.radar_stats.h+gap)};
    v.list_rows={v.content.x,v.content.y,v.content.w,v.content.h*72/100};
    v.list_summary={v.content.x,v.list_rows.y+v.list_rows.h+gap,v.content.w,
                    v.content.y+v.content.h-(v.list_rows.y+v.list_rows.h+gap)};
    const int item_h=std::max(44,v.list_rows.h/6);
    for(int i=0;i<6;++i)v.list_items[i]={v.list_rows.x,v.list_rows.y+i*item_h,v.list_rows.w,item_h-2};
    v.list_lock={v.list_summary.x+v.list_summary.w-112,v.list_summary.y+4,104,44};
    v.target_identity={v.content.x,v.content.y,v.content.w,v.content.h*36/100};
    v.target_lock={v.target_identity.x+v.target_identity.w-120,v.target_identity.y+4,112,44};
    v.target_atc_button={v.target_identity.x+8,v.target_identity.y+v.target_identity.h-52,
                         v.target_identity.w-16,44};
    v.target_metrics={v.content.x,v.target_identity.y+v.target_identity.h+gap,v.content.w,
                      v.content.h-v.target_identity.h-gap};
    v.target_signal={v.target_metrics.x,v.target_metrics.y+v.target_metrics.h-74,v.target_metrics.w,74};
    v.target_map_button={v.target_signal.x+v.target_signal.w-156,v.target_signal.y+12,148,48};
    const int cw=(v.content.w-gap)/2,ch=(v.content.h*34/100-gap)/2;
    for(int i=0;i<3;++i)v.stats_charts[i]={v.content.x+(i%2)*(cw+gap),v.content.y+(i/2)*(ch+gap),cw,ch};
    for(int i=0;i<5;++i)v.stats_metrics[i]={v.content.x,v.content.y+v.content.h*35/100+i*42,v.content.w,38};
    for(int i=0;i<4;++i)v.stats_data[i]={v.content.x+(i%2)*(cw+gap),v.content.y+v.content.h*63/100+(i/2)*58,cw,52};
    v.settings_main=v.content;
    v.settings_open_data={v.settings_main.x+v.settings_main.w/2,v.settings_main.y+v.settings_main.h/2,
                          v.settings_main.w/2,v.settings_main.h/2};
    for(int i=0;i<4;++i)v.settings_data[i]={v.content.x+i*(v.content.w/4),v.content.y+v.content.h-48,
                                             v.content.w/4-4,44};
    v.latitude={v.content.x,v.content.y+56,v.content.w,48};
    v.longitude={v.content.x,v.latitude.y+56,v.content.w,48};
    v.range={v.content.x,v.longitude.y+56,v.content.w,48};
    v.gain={v.content.x,v.range.y+58,std::min(140,v.content.w),48};
    v.exit={v.content.x+v.content.w-120,v.content.y+4,112,44};
    const int z=48;v.map_zoom_in={v.radar_map.x+v.radar_map.w-z-8,v.radar_map.y+80,z,z};
    v.map_zoom_out={v.map_zoom_in.x,v.map_zoom_in.y+z+6,z,z};
    v.map_labels={v.radar_map.x+8,v.radar_map.y+80,v.radar_map.w-2*z-26,48};
    v.keypad=v.content;v.keypad_value={v.content.x+8,v.content.y+56,v.content.w-16,48};
    v.keypad_cancel={v.content.x+8,v.content.y+v.content.h-52,(v.content.w-24)/2,44};
    v.keypad_save={v.keypad_cancel.x+v.keypad_cancel.w+8,v.keypad_cancel.y,v.keypad_cancel.w,44};
    for(int i=0;i<13;++i){const int kw=(v.content.w-32)/4;v.keypad_keys[i]={v.content.x+8+(i%4)*(kw+4),v.content.y+112+(i/4)*52,kw,48};}
  } else {
    const int left=v.content.w*64/100, bottom=v.content.y+v.content.h;
    v.radar_map={v.content.x,v.content.y,left-8,v.content.h*70/100};
    v.radar_map_area={v.radar_map.x+8,v.radar_map.y+64,v.radar_map.w-16,v.radar_map.h-76};
    v.radar_status={v.content.x+left,v.content.y,v.content.w-left,v.content.h*35/100};
    v.radar_stats={v.content.x,v.radar_map.y+v.radar_map.h+gap,left-8,bottom-(v.radar_map.y+v.radar_map.h+gap)};
    v.radar_list={v.content.x+left,v.radar_status.y+v.radar_status.h+gap,v.content.w-left,
                  bottom-(v.radar_status.y+v.radar_status.h+gap)};
    v.list_rows=v.content;
    v.list_summary={};
    const int item_h=std::max(44,v.list_rows.h/6);
    for(int i=0;i<6;++i)v.list_items[i]={v.list_rows.x,v.list_rows.y+i*item_h,v.list_rows.w,item_h-2};
    v.list_lock={v.list_rows.x+v.list_rows.w-112,v.list_rows.y+4,104,44};
    v.target_identity={v.content.x,v.content.y,v.content.w*40/100,v.content.h};
    v.target_lock={v.target_identity.x+v.target_identity.w-120,v.target_identity.y+4,112,44};
    v.target_atc_button={v.target_identity.x+8,v.target_identity.y+v.target_identity.h-52,
                         v.target_identity.w-16,44};
    v.target_metrics={v.target_identity.x+v.target_identity.w+gap,v.content.y,v.content.w-v.target_identity.w-gap,v.content.h};
    v.target_signal={v.target_metrics.x,v.target_metrics.y+v.target_metrics.h-74,v.target_metrics.w,74};
    v.target_map_button={v.target_signal.x+v.target_signal.w-156,v.target_signal.y+12,148,48};
    const int cw=(v.content.w-2*gap)/3,ch=(v.content.h-2*gap)/3;
    for(int i=0;i<3;++i)v.stats_charts[i]={v.content.x+i*(cw+gap),v.content.y,cw,ch};
    for(int i=0;i<5;++i)v.stats_metrics[i]={v.content.x+i*(v.content.w/5),v.content.y+ch+gap,v.content.w/5-gap,72};
    const int data_w=(v.content.w-3*gap)/4;
    for(int i=0;i<4;++i)v.stats_data[i]={v.content.x+i*(data_w+gap),v.content.y+v.content.h-52,data_w,48};
    v.settings_main=v.content;
    v.settings_open_data={v.settings_main.x+v.settings_main.w/2,v.settings_main.y+v.settings_main.h/2,
                          v.settings_main.w/2,v.settings_main.h/2};
    for(int i=0;i<4;++i)v.settings_data[i]={v.content.x+i*(v.content.w/4),v.content.y+v.content.h-48,
                                             v.content.w/4-4,44};
    const int row=56;v.latitude={v.content.x,v.content.y+56,v.content.w/2-gap,row};
    v.longitude={v.content.x+v.content.w/2+gap,v.latitude.y,v.content.w/2-gap,row};
    v.range={v.latitude.x,v.latitude.y+row+gap,v.latitude.w,row};v.gain={v.longitude.x,v.range.y,v.longitude.w,row};
    v.exit={v.content.x+v.content.w-120,v.content.y+4,112,44};
    const int z=48;v.map_zoom_in={v.radar_map.x+v.radar_map.w-z-8,v.radar_map.y+80,z,z};
    v.map_zoom_out={v.map_zoom_in.x,v.map_zoom_in.y+z+6,z,z};
    v.map_labels={v.radar_map.x+8,v.radar_map.y+80,160,48};
  }
  v.keypad={safe.x+8,safe.y+4,safe.w-16,safe.h-8};
  v.keypad_value={v.keypad.x+8,v.keypad.y+52,v.keypad.w-16,48};
  v.keypad_cancel={v.keypad.x+8,v.keypad.y+v.keypad.h-52,(v.keypad.w-24)/2,44};
  v.keypad_save={v.keypad_cancel.x+v.keypad_cancel.w+8,v.keypad_cancel.y,v.keypad_cancel.w,44};
  for(int i=0;i<13;++i){const int kw=(v.keypad.w-32)/4;v.keypad_keys[i]={v.keypad.x+8+(i%4)*(kw+4),v.keypad.y+112+(i/4)*52,kw,48};}
  v.valid=contains(safe,v.content);
  for(auto r:v.tabs)v.valid=v.valid&&contains(safe,r)&&r.w>=44&&r.h>=44;
  return v;
}
} // namespace orcsdr::adsb::layout
