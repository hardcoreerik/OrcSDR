#pragma once

#include "dashboard_header_layout.hpp"

#include <algorithm>

namespace orcsdr::airband::layout {
using Rect = audio_header::HeaderRect;

inline bool inside(Rect outer, Rect r) {
  return r.w > 0 && r.h > 0 && r.x >= outer.x && r.y >= outer.y &&
         r.x + r.w <= outer.x + outer.w && r.y + r.h <= outer.y + outer.h;
}
inline bool overlap(Rect a, Rect b) {
  return a.x < b.x + b.w && b.x < a.x + a.w &&
         a.y < b.y + b.h && b.y < a.y + a.h;
}

struct Layout {
  bool valid = false, legacy = false, portrait = false;
  Rect viewport{}, content{}, tabs{}, tab[6];
  Rect detail_overlay{},detail_close{};
  Rect listen_now{}, listen_controls{}, listen_status{}, listen_gain{};
  Rect control[9], gain_auto{}, gain_down{}, gain_value{}, gain_up{}, rtl_agc{};
  Rect scan_status{}, scan_toggle{}, scan_rows[8], scan_previous{}, scan_next{}, scan_page{};
  int scan_row_count = 0;
  Rect airport_radius{}, airport_reload{}, airport_rows[12], airport_previous{}, airport_next{}, airport_page{};
  int airport_row_count = 0;
  Rect activity_clear{}, activity_rows[12], activity_previous{}, activity_next{}, activity_page{};
  int activity_row_count = 0;
  Rect setup_row[7], setup_minus[7], setup_value[7], setup_plus[7];
  Rect setup_previous{},setup_next{};
  int setup_first=0,setup_visible_count=7,setup_page_count=1;
  Rect scope_title{}, scope_palette{}, scope_speed{}, scope_plot{}, scope_axis{}, scope_waterfall{}, scope_status{};
  Rect scope_control[15];
  int scope_control_count = 0, scope_plot_width = 0, scope_plot_height = 0;
};

inline Rect tab_rect(Rect bar, int i, bool portrait) {
  if (portrait) {
    constexpr int gap = 4;
    const int row = i / 3, col = i % 3;
    const int cell_w = (bar.w - 2 * gap) / 3;
    const int cell_h = (bar.h - gap) / 2;
    const int x = bar.x + col * (cell_w + gap)+4, y = bar.y + row * (cell_h + gap)+4;
    const int width=col==2?bar.x+bar.w-(x-4):cell_w;
    const int height=row==1?bar.y+bar.h-(y-4):cell_h;
    return {x,y,width-8,height-8};
  }
  const int cell_w=bar.w/6,x=bar.x+i*cell_w+4;
  return {x,bar.y+4,(i==5?bar.x+bar.w-(x-4):cell_w)-8,bar.h-8};
}

inline Layout make_layout(Rect v) {
  Layout l{}; l.viewport = v; l.portrait = v.h > v.w;
  if (v.x == 0 && v.y == 0 && v.w == 1280 && v.h == 720) {
    l.valid = l.legacy = true; l.content = {0, 93, 1280, 537}; l.tabs = {0, 630, 1280, 90};
    l.listen_now = {24,108,520,286}; l.listen_controls = {556,108,700,286};
    l.listen_status = {24,404,1232,112}; l.listen_gain = {24,524,1232,96};
    for (int r=0;r<3;++r) for(int c=0;c<3;++c)
      l.control[r*3+c]={574+c*220,126+r*82,208,70};
    l.gain_auto={44,540,270,64};l.gain_down={330,540,120,64};l.gain_value={460,540,230,64};
    l.gain_up={700,540,120,64};l.rtl_agc={850,540,250,64};
    l.scan_status={24,178,1232,104};l.scan_toggle={958,194,278,66};
    l.scan_row_count=5;for(int i=0;i<5;++i)l.scan_rows[i]={24,330+i*54,1232,48};
    l.scan_previous={820,284,60,44};l.scan_next={1180,284,64,44};l.scan_page={888,284,280,44};
    l.airport_radius={700,114,274,58};l.airport_reload={990,114,266,58};
    l.airport_row_count=7; l.activity_row_count=7;
    for(int i=0;i<7;++i){l.airport_rows[i]={24,194+i*56,1232,49};l.activity_rows[i]={24,194+i*56,1232,49};}
    l.airport_previous={24,586,64,44};l.airport_next={1192,586,64,44};l.airport_page={96,586,1088,44};
    l.activity_clear={1010,114,246,58};l.activity_previous=l.airport_previous;l.activity_next=l.airport_next;l.activity_page=l.airport_page;
    for(int i=0;i<7;++i){const int y=112+i*70;l.setup_row[i]={24,y,1232,63};l.setup_minus[i]={780,y+8,104,50};l.setup_value[i]={896,y+8,232,50};l.setup_plus[i]={1140,y+8,104,50};}
    l.scope_title={24,100,860,36};l.scope_palette={900,100,170,52};l.scope_speed={1086,100,170,52};
    l.scope_plot={24,168,1232,150};l.scope_axis={24,318,1232,20};l.scope_waterfall={24,340,1232,150};
    l.scope_status={24,138,860,26}; l.scope_plot_width=l.scope_plot.w;l.scope_plot_height=l.scope_plot.h;
    const int groups[3]={24,428,832}; const int ys[2]={498,558};
    l.scope_control[0]={24,498,64,52};l.scope_control[1]={336,498,64,52};l.scope_control[2]={428,498,64,52};l.scope_control[3]={740,498,64,52};l.scope_control[4]={832,498,64,52};l.scope_control[5]={1144,498,64,52};
    l.scope_control[6]={24,558,64,52};l.scope_control[7]={336,558,64,52};l.scope_control[8]={428,558,64,52};l.scope_control[9]={740,558,64,52};
    l.scope_control[10]={832,558,160,52};l.scope_control[11]={998,558,110,52};l.scope_control[12]={1114,558,94,52};
    l.scope_control[13]=l.scope_palette;l.scope_control[14]=l.scope_speed;l.scope_control_count=15;
    for(int i=0;i<6;++i) l.tab[i]={i*213+4,634,205,82};
    return l;
  } else {
    const auto h=audio_header::make_header_layout(v);if(!h.valid||v.w>1232)return l;
    constexpr int gap=8,touch=48,pad=8;
    l.tabs={v.x,v.y+v.h-(l.portrait?112:56),v.w,l.portrait?112:56};
    for(int i=0;i<6;++i)l.tab[i]=tab_rect(l.tabs,i,l.portrait);
    l.content={v.x,h.content_top,v.w,l.tabs.y-h.content_top-gap};
    if(l.content.h<144||l.content.w<360)return l;
    const int x=v.x+pad,y=l.content.y+pad,w=v.w-2*pad,hh=l.content.h-2*pad;
    // Listen's cards keep their original hierarchy while collapsing to a vertical stack.
    if(l.portrait){
      const int row=(hh-3*gap)/4;
      l.listen_now={x,y,w,std::max(144,row)};l.listen_controls={x,y+row+gap,w,row};
      l.listen_status={x,y+2*(row+gap),w,row};l.listen_gain={x,y+3*(row+gap),w,row};
      const int bw=(w-2*gap)/3,bh=std::max(touch,(row-3*gap)/3);
      for(int r=0;r<3;++r)for(int c=0;c<3;++c)l.control[r*3+c]={x+c*(bw+gap),l.listen_controls.y+gap+r*(bh+gap),bw,bh};
      l.gain_auto={x+gap,l.listen_gain.y+gap,(w-3*gap)/2,touch};l.gain_down={x+gap,l.listen_gain.y+gap+touch+gap,touch,touch};
      l.gain_value={l.gain_down.x+touch+gap,l.gain_down.y,w-2*(touch+gap),touch};
      l.gain_up={l.gain_value.x+l.gain_value.w+gap,l.gain_value.y,touch,touch};l.rtl_agc={x+w/2+gap,l.gain_auto.y,w/2-2*gap,touch};
    }else{
      if(v.h<480){
        const int gh=44;
        l.listen_now={x,y,w,72};l.listen_controls={x,y+72+gap,w,3*gh+2*2};
        l.listen_status={x,y,0,0};l.listen_gain={x,l.listen_controls.y+l.listen_controls.h+gap,w,48};
        const int bw=(w-2*gap)/3;
        for(int r=0;r<3;++r)for(int c=0;c<3;++c)l.control[r*3+c]={x+c*(bw+gap),l.listen_controls.y+r*(gh+2),bw,gh};
        const int gw=(w-6*gap)/5;l.gain_auto={x+gap,l.listen_gain.y+4,gw*2,touch};l.gain_down={l.gain_auto.x+l.gain_auto.w+gap,l.gain_auto.y,gw,touch};
        l.gain_value={l.gain_down.x+gw+gap,l.gain_auto.y,gw,touch};l.gain_up={l.gain_value.x+gw+gap,l.gain_auto.y,gw,touch};l.rtl_agc={l.gain_up.x+gw+gap,l.gain_auto.y,gw,touch};
      } else {
      const int main_h=std::min(170,hh/2);l.listen_now={x,y,w*42/100,main_h};
      l.listen_controls={l.listen_now.x+l.listen_now.w+gap,y,w-l.listen_now.w-gap,main_h};
      l.listen_status={x,y+main_h+gap,w,std::max(56,(hh-main_h-2*gap)/2)};
      l.listen_gain={x,l.listen_status.y+l.listen_status.h+gap,w,hh-(l.listen_status.y-y)-l.listen_status.h-gap};
      const int bw=(l.listen_controls.w-2*gap)/3,bh=(main_h-3*gap)/3;
      for(int r=0;r<3;++r)for(int c=0;c<3;++c)l.control[r*3+c]={l.listen_controls.x+c*(bw+gap),y+gap+r*(bh+gap),bw,bh};
      }
      const int gw=(w-6*gap)/5;l.gain_auto={x+gap,l.listen_gain.y+gap,gw*2,touch};l.gain_down={l.gain_auto.x+l.gain_auto.w+gap,l.gain_auto.y,gw,touch};
      l.gain_value={l.gain_down.x+gw+gap,l.gain_auto.y,gw,touch};l.gain_up={l.gain_value.x+gw+gap,l.gain_auto.y,gw,touch};l.rtl_agc={l.gain_up.x+gw+gap,l.gain_auto.y,gw,touch};
    }
    const int scan_y=y; l.scan_status={x,scan_y,w,std::min(108,hh/3)};l.scan_toggle={x+w-touch,scan_y+gap,w?touch:0,touch};
    const int rows_y=scan_y+l.scan_status.h+gap, rows_h=hh-l.scan_status.h-gap-touch-gap;
    l.scan_row_count=std::clamp((rows_h+gap)/(touch+gap),1,8);const int sh=(rows_h-gap*(l.scan_row_count-1))/l.scan_row_count;
    for(int i=0;i<l.scan_row_count;++i)l.scan_rows[i]={x,rows_y+i*(sh+gap),w,sh};
    l.scan_previous={x,y+hh-touch,touch,touch};l.scan_next={x+w-touch,y+hh-touch,touch,touch};l.scan_page={x+touch+gap,y+hh-touch,w-2*(touch+gap),touch};
    l.airport_radius={x,y,w/2-touch-gap,touch};l.airport_reload={x+w/2+gap,y,w/2-gap,touch};
    l.activity_clear=l.airport_reload;
    const int list_y=y+touch+gap,list_h=hh-touch-2*gap;
    l.airport_row_count=l.activity_row_count=std::clamp(list_h/(touch+gap),1,12);
    const int rh=(list_h-gap*(l.airport_row_count-1))/l.airport_row_count;
    for(int i=0;i<l.airport_row_count;++i){l.airport_rows[i]={x,list_y+i*(rh+gap),w,rh};l.activity_rows[i]=l.airport_rows[i];}
    l.airport_previous={x,y+hh-touch,touch,touch};l.airport_next={x+w-touch,y+hh-touch,touch,touch};l.airport_page={x+touch+gap,y+hh-touch,w-2*(touch+gap),touch};
    l.activity_previous=l.airport_previous;l.activity_next=l.airport_next;l.activity_page=l.airport_page;
    const int sy=y;
    const bool setup_paged=!l.portrait;
    l.setup_visible_count=setup_paged?3:7;l.setup_page_count=setup_paged?3:1;
    l.setup_previous={x, y+hh-touch,touch,touch};l.setup_next={x+w-touch,y+hh-touch,touch,touch};
    if(setup_paged){l.setup_first=0;const int shh=std::max(64,(hh-touch-4*gap)/3);for(int i=0;i<3;++i){l.setup_row[i]={x,sy+i*(shh+gap),w,shh};const int cw=std::min(120,(w-2*gap)/3);const int cy=l.setup_row[i].y+(shh-touch)/2;l.setup_minus[i]={x+w-3*cw-2*gap,cy,cw,touch};l.setup_value[i]={l.setup_minus[i].x+cw+gap,cy,cw,touch};l.setup_plus[i]={l.setup_value[i].x+cw+gap,cy,cw,touch};}}
    else {const int shh=l.portrait?std::max(touch,(hh-6*gap)/7):44;const int row_gap=l.portrait?gap:2;for(int i=0;i<7;++i){l.setup_row[i]={x,sy+i*(shh+row_gap),w,shh};const int cw=std::min(120,(w-2*gap)/3);const int cy=l.portrait?l.setup_row[i].y+shh-touch:l.setup_row[i].y;l.setup_minus[i]={x+w-3*cw-2*gap,cy,cw,touch};l.setup_value[i]={l.setup_minus[i].x+cw+gap,cy,cw,touch};l.setup_plus[i]={l.setup_value[i].x+cw+gap,cy,cw,touch};}}
    const int title_h=touch;l.scope_title={x,y,w-2*touch-2*gap,title_h};l.scope_palette={x+w-2*touch-2*gap,y,touch*2,touch};l.scope_speed={x+w-touch,y,touch,touch};
    const bool compact=v.h<480;const int control_touch=compact?44:touch;
    const int controls_h=compact?3*control_touch+2*4:3*touch+2*gap, title_height=compact?40:touch;
    const int plot_top=y+title_height+(compact?4:gap);
    const int plot_h=compact?30:std::max(72,(hh-title_height-controls_h-4*gap)/3);
    l.scope_title={x,y,compact?w:w-2*touch-2*gap,title_height};
    l.scope_plot={x,plot_top,w,plot_h};l.scope_axis={x,plot_top+plot_h,w,compact?16:24};
    l.scope_waterfall={x,l.scope_axis.y+l.scope_axis.h+(compact?4:gap),w,plot_h};
    l.scope_status=compact?Rect{}:Rect{x,l.scope_waterfall.y+l.scope_waterfall.h+gap,w,24};
    l.scope_plot_width=w;l.scope_plot_height=plot_h;
    const int controls_y=l.content.y+l.content.h-controls_h;const int cell=(w-4*gap)/5;
    for(int i=0;i<15;++i)l.scope_control[i]={x+(i%5)*(cell+gap),controls_y+(i/5)*(control_touch+(compact?4:gap)),cell,control_touch};l.scope_control_count=15;
  }
  const int overlay_margin=std::min(24,std::max(8,v.w/24));
  l.detail_overlay={l.content.x+overlay_margin,l.content.y+overlay_margin,l.content.w-2*overlay_margin,l.content.h-2*overlay_margin};
  l.detail_close={l.detail_overlay.x+l.detail_overlay.w-56,l.detail_overlay.y+8,48,48};
  l.valid=audio_header::header_inside(v,l.content)&&audio_header::header_inside(v,l.tabs);
  for(auto r:l.tab)l.valid=l.valid&&audio_header::header_inside(l.tabs,r)&&r.w>=44&&r.h>=44;
  for(auto r:{l.listen_now,l.listen_controls,l.listen_status,l.listen_gain,l.scan_status,l.scope_plot,l.scope_waterfall})if(r.w>0&&r.h>0)l.valid=l.valid&&audio_header::header_inside(l.content,r);
  for(auto r:l.control)l.valid=l.valid&&audio_header::header_inside(l.content,r)&&r.w>=44&&r.h>=44;
  for(int i=0;i<l.setup_visible_count;++i){l.valid=l.valid&&audio_header::header_inside(l.content,l.setup_row[i])&&audio_header::header_inside(l.content,l.setup_value[i])&&l.setup_value[i].w>=44&&l.setup_value[i].h>=44;}
  if(l.setup_page_count>1)l.valid=l.valid&&audio_header::header_inside(l.content,l.setup_previous)&&audio_header::header_inside(l.content,l.setup_next);
  for(int i=0;i<l.scope_control_count;++i)l.valid=l.valid&&audio_header::header_inside(l.content,l.scope_control[i])&&l.scope_control[i].w>=44&&l.scope_control[i].h>=44;
  return l;
}
} // namespace orcsdr::airband::layout
