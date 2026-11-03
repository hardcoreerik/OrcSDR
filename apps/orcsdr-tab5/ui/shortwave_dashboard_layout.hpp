#pragma once

#include "dashboard_header_layout.hpp"

#include <algorithm>

namespace orcsdr::shortwave::layout {
using Rect = audio_header::HeaderRect;

struct Layout {
  Rect viewport{}, content{}, header_status{}, tabs{};
  Rect tab[5]{};
  Rect frequency{}, frequency_minus{}, frequency_value{}, frequency_plus{};
  Rect quick[12]{};
  Rect spectrum{}, waterfall{}, live_status{}, zoom_minus{}, span_value{}, zoom_plus{};
  Rect drawer{}, drawer_close{}, tuner_agc{}, rtl_agc{}, gain_slider{};
  Rect page_title{}, page_subtitle{}, storage_status{}, hunt_progress{};
  Rect page_controls[5]{}, rows[12]{}, row_actions[12][4]{};
  Rect hunt_previous{},hunt_page{},hunt_next{};
  int row_count = 0,hunt_row_count=0,onair_row_count=0;
  bool valid = false, legacy = false, portrait = false;
};

inline Rect inset(Rect r, int x, int y, int w, int h) { return {r.x+x,r.y+y,w,h}; }
inline Rect cell(Rect r, int i, int n, int gap = 8) {
  const int w = (r.w - (n-1)*gap)/n;
  return {r.x+i*(w+gap),r.y,w,r.h};
}

inline Layout make_layout(Rect v) {
  Layout l{}; l.viewport=v; l.portrait=v.h>v.w;
  l.legacy=v.x==0&&v.y==0&&v.w==1280&&v.h==720;
  if (l.legacy) {
    l.valid=true; l.content={0,93,1280,537}; l.header_status={420,12,446,54};
    l.tabs={0,630,1280,90};
    for(int i=0;i<5;++i) l.tab[i]={i*256+4,634,248,82};
    l.frequency={24,104,1232,80}; l.frequency_minus={42,112,64,64};
    l.frequency_value={120,108,1040,72}; l.frequency_plus={1174,112,64,64};
    const Rect quick[]={{24,190,130,52},{162,190,52,52},{222,190,150,52},
      {380,190,52,52},{440,190,100,52},{548,190,86,52},{642,190,86,52},
      {736,190,96,52},{840,190,96,52},{944,190,130,52},{1082,190,174,52}};
    for(int i=0;i<11;++i)l.quick[i]=quick[i];
    l.spectrum={24,276,1232,204};l.waterfall={24,486,1232,102};l.live_status={24,246,1232,26};
    l.zoom_minus={24,592,160,34};l.span_value={184,592,912,34};l.zoom_plus={1096,592,160,34};
    l.drawer={840,104,420,522};l.drawer_close={858,552,378,52};
    l.tuner_agc={858,176,184,68};l.rtl_agc={1052,176,184,68};l.gain_slider={872,442,350,22};
    l.page_title={36,104,700,44};l.page_subtitle={38,150,900,32};l.storage_status={780,109,440,42};l.hunt_progress={1080,146,164,34};
    for(int i=0;i<5;++i)l.page_controls[i]={36+i*182,170,174,58};
    l.row_count=7;
    for(int i=0;i<7;++i){int y=250+i*50;l.rows[i]={36,y,1208,42};
      l.row_actions[i][0]={36,y,780,42};l.row_actions[i][1]={826,y,100,42};
      l.row_actions[i][2]={936,y,130,42};l.row_actions[i][3]={1076,y,168,42};}
    l.hunt_row_count=6;l.onair_row_count=6;l.hunt_previous={36,580,70,44};l.hunt_page={114,580,1052,44};l.hunt_next={1174,580,70,44};
    return l;
  }
  if(v.w<=0||v.h<=0)return l;
  const auto h=audio_header::make_header_layout(v);
  if(!h.valid||v.w>1280)return l;
  l.header_status=h.status;
  const int pad=8,gap=8,touch=44;
  const int tabh=l.portrait?96:48;
  l.tabs={v.x,v.y+v.h-tabh,v.w,tabh};
  if(l.portrait){const int rh=(tabh-gap)/2;for(int i=0;i<5;++i){int row=i<3?0:1,col=i<3?i:i-3,n=row?2:3;int cw=(v.w-gap*(n-1))/n;l.tab[i]={v.x+col*(cw+gap),l.tabs.y+row*(rh+gap),col==n-1?v.w-col*(cw+gap):cw,row?l.tabs.y+tabh-(l.tabs.y+row*(rh+gap)):rh};}}
  else for(int i=0;i<5;++i){int x=v.x+i*v.w/5;l.tab[i]={x,l.tabs.y,(i==4?v.x+v.w:v.x+(i+1)*v.w/5)-x,tabh};}
  l.content={v.x,h.content_top,v.w,l.tabs.y-h.content_top-4};
  if(l.content.h<160)return l;
  const int x=v.x+pad,w=v.w-2*pad,top=l.content.y+pad,bottom=l.content.y+l.content.h-pad;
  if(l.portrait){
    l.frequency={x,top,w,80};l.frequency_minus={x,top,54,64};l.frequency_plus={x+w-54,top,54,64};l.frequency_value={x+58,top,w-116,72};
    l.quick[0]={x,top+86,w,48};
    for(int i=1;i<11;++i){int row=(i-1)/2,col=(i-1)%2;int hh=48;l.quick[i]={x+col*(w/2+gap/2),top+140+row*(hh+gap),w/2-gap/2,hh};}
    const int plot_y=std::min(bottom-210,top+140+5*56+16);
    l.spectrum={x,plot_y,w,std::min(224,std::max(110,(bottom-plot_y-104)*2/3))};
    l.waterfall={x,plot_y+l.spectrum.h+gap,w,std::max(72,bottom-(plot_y+l.spectrum.h+gap)-48)};
    l.live_status={x,plot_y-18,w,16};
    l.zoom_minus={x,bottom-44,80,44};l.span_value={x+88,bottom-44,w-176,44};l.zoom_plus={x+w-80,bottom-44,80,44};
  } else {
    const int mid=top+60;l.frequency={x,top,w,56};
    l.frequency_minus={x,top,56,56};l.frequency_plus={x+w-56,top,56,56};l.frequency_value={x+64,top,w-128,56};
    for(int i=0;i<11;++i){int col=i%6,row=i/6;int cw=(w-5*gap)/6;l.quick[i]={x+col*(cw+gap),mid+row*48,cw,44};}
    const int plot_y=mid+110,chart_h=std::max(68,bottom-44-plot_y-8),chart_w=(w-gap)/2;
    l.spectrum={x,plot_y,chart_w,chart_h};l.waterfall={x+chart_w+gap,plot_y,w-chart_w-gap,chart_h};
    l.live_status={x,plot_y-18,w,16};
    l.zoom_minus={x,bottom-44,104,44};l.span_value={x+112,bottom-44,w-224,44};l.zoom_plus={x+w-104,bottom-44,104,44};
  }
  l.drawer=l.portrait?Rect{x,top,w,bottom-top}:Rect{x+w*2/3,top,w/3,bottom-top};l.drawer_close={l.drawer.x+gap,bottom-touch,l.drawer.w-2*gap,touch};
  l.tuner_agc={l.drawer.x+gap,l.drawer.y+72,(l.drawer.w-3*gap)/2,touch};l.rtl_agc={l.tuner_agc.x+l.tuner_agc.w+gap,l.tuner_agc.y,l.tuner_agc.w,touch};
  l.gain_slider={l.drawer.x+gap,bottom-104,l.drawer.w-2*gap,22};
  l.page_title={x,top,w/2,44};l.page_subtitle={x,top+44,w,44};l.storage_status={x+w/2,top,w/2,44};l.hunt_progress={x+w-192,top,192,44};
  l.row_count=std::clamp((bottom-(top+204))/(touch+gap),1,12);
  for(int i=0;i<l.row_count;++i){int y=top+204+i*(touch+gap);int rh=std::min(touch,bottom-y);l.rows[i]={x,y,w,rh};
    const int a=w*58/100,b=w*14/100,c=w*14/100;
    l.row_actions[i][0]={x,y,a-2*gap,rh};l.row_actions[i][1]={x+a,y,b-gap,rh};
    l.row_actions[i][2]={x+a+b,y,c-gap,rh};l.row_actions[i][3]={x+a+b+c,y,w-a-b-c,rh};}
  for(int i=0;i<5;++i)l.page_controls[i]=cell({x,top+96,w,touch},i,5,gap);
  l.page_controls[0]={x,top+96,touch,touch};
  l.page_controls[1]={x+touch+gap,top+96,w-2*touch-2*gap,touch};
  l.page_controls[2]={x+w-touch,top+96,touch,touch};
  l.page_controls[3]={x,top+148,(w-gap)/2,touch};
  l.page_controls[4]={x+(w+gap)/2,top+148,(w-gap)/2,touch};
  l.hunt_row_count=std::clamp((bottom-(top+204)-(touch+gap))/(touch+gap),1,12);
  l.onair_row_count=l.hunt_row_count;
  l.hunt_previous={x,bottom-touch,touch,touch};l.hunt_page={x+touch+gap,bottom-touch,w-2*(touch+gap),touch};l.hunt_next={x+w-touch,bottom-touch,touch,touch};
  l.valid=audio_header::header_inside(v,l.content)&&audio_header::header_inside(v,l.tabs);
  for(auto r:l.tab)l.valid=l.valid&&r.w>=touch&&r.h>=touch&&audio_header::header_inside(l.tabs,r);
  const Rect req[]={l.frequency,l.spectrum,l.waterfall,l.zoom_minus,l.zoom_plus,l.tabs,l.content};
  for(auto r:req)l.valid=l.valid&&audio_header::header_inside(v,r);
  for(int i=0;i<11;++i)l.valid=l.valid&&l.quick[i].w>=touch&&l.quick[i].h>=touch&&audio_header::header_inside(l.content,l.quick[i]);
  for(const auto r:{l.frequency_minus,l.frequency_plus,l.zoom_minus,l.zoom_plus,l.drawer_close,l.tuner_agc,l.rtl_agc,l.hunt_previous,l.hunt_next})
    l.valid=l.valid&&r.w>=touch&&r.h>=touch&&audio_header::header_inside(v,r);
  for(const auto r:l.page_controls)l.valid=l.valid&&r.w>=touch&&r.h>=touch&&audio_header::header_inside(l.content,r);
  l.valid=l.valid&&audio_header::header_inside(v,l.drawer)&&l.drawer.w>=touch&&l.drawer.h>=touch&&
      audio_header::header_inside(v,l.hunt_page)&&l.hunt_page.w>=touch&&l.hunt_page.h>=touch&&
      audio_header::header_inside(l.content,l.live_status)&&l.live_status.h>=14;
  for(int i=0;i<l.row_count;++i)for(const auto r:l.row_actions[i])
    l.valid=l.valid&&r.w>=touch&&r.h>=touch&&audio_header::header_inside(l.content,r);
  return l;
}
} // namespace orcsdr::shortwave::layout
