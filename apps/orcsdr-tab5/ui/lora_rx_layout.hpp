#pragma once
#include "lora_dashboard.hpp"
#include <algorithm>
namespace orcsdr::lora::rx_layout {
using Rect=audio_header::HeaderRect;
inline Rect inset(Rect r,int n=8){return {r.x+n,r.y+n,std::max(0,r.w-2*n),std::max(0,r.h-2*n)};}
inline Rect cell(Rect r,int index,int columns,int rows,int gap=8){
  const int col=index%columns,row=index/columns;
  const int left=r.x+col*(r.w+gap)/columns,top=r.y+row*(r.h+gap)/rows;
  const int right=r.x+(col+1)*(r.w+gap)/columns-gap,bottom=r.y+(row+1)*(r.h+gap)/rows-gap;
  return {left,top,right-left,bottom-top};
}
inline bool hit(Rect r,int x,int y){return r.w>0&&r.h>0&&x>=r.x&&y>=r.y&&x<r.x+r.w&&y<r.y+r.h;}
struct Layout {
  Rect viewport{},header_status{},tabs{},tab[6]{},actions[6]{},body{},metric[6]{};
  Rect plot{},waterfall{},events{},nodes_list{},node_details{},node_rows[6]{},node_previous{},node_next{};
  Rect traffic_rows[6]{},traffic_previous{},traffic_next{},map{},map_info{},map_zoom_in{},map_zoom_out{};
  Rect channel_dialog{},channel_close{},region_previous{},region_next{},slot_previous{},slot_next{};
  int tab_count=5,action_count=4,metric_count=6,node_row_count=0,traffic_row_count=0;
  bool vertical=false,single_node_panel=false,valid=false;
};
inline Layout make(Rect v,View view,bool has_tx) {
  Layout l{};l.viewport=v;l.vertical=v.h>v.w;l.tab_count=has_tx?6:5;
  const auto header=audio_header::make_header_layout(v);if(!header.valid)return l;
  l.header_status=header.status;
  const int tab_rows=l.vertical?2:1,tab_columns=l.vertical?3:l.tab_count;
  const int tab_h=l.vertical?48:(v.h>=600?80:48);
  l.tabs={v.x,v.y+v.h-tab_rows*tab_h,v.w,tab_rows*tab_h};
  for(int i=0;i<l.tab_count;++i)l.tab[i]=cell(l.tabs,i,tab_columns,tab_rows,0);
  l.action_count=(view==View::map||view==View::overview)?6:4;
  const int columns=l.vertical?(view==View::map?3:2):l.action_count;
  const int rows=(l.action_count+columns-1)/columns;
  const Rect action_area={v.x+8,l.tabs.y-8-rows*48-(rows-1)*8,v.w-16,rows*48+(rows-1)*8};
  for(int i=0;i<l.action_count;++i)l.actions[i]=cell(action_area,i,columns,rows);
  l.body={v.x+8,header.content_top+8,v.w-16,action_area.y-header.content_top-16};
  if(l.body.h<120)return l;
  const int metric_columns=l.vertical?(v.w>=420?3:2):6;
  if(l.vertical&&l.body.h<300)l.metric_count=3;
  const int metric_rows=(l.metric_count+metric_columns-1)/metric_columns;
  const Rect metrics={l.body.x,l.body.y,l.body.w,metric_rows*54+(metric_rows-1)*8};
  for(int i=0;i<l.metric_count;++i)l.metric[i]=cell(metrics,i,metric_columns,metric_rows);
  const Rect monitor={l.body.x,metrics.y+metrics.h+8,l.body.w,l.body.h-metrics.h-8};
  if(l.vertical) {
    l.plot={monitor.x,monitor.y,monitor.w,std::max(48,monitor.h*45/100)};
    l.waterfall={monitor.x,l.plot.y+l.plot.h+8,monitor.w,std::max(24,monitor.h*25/100)};
    const int end=l.waterfall.y+l.waterfall.h+8;
    if(monitor.y+monitor.h-end>=70)l.events={monitor.x,end,monitor.w,monitor.y+monitor.h-end};
  } else {
    const int plot_w=(monitor.w-8)*7/10;
    l.plot={monitor.x,monitor.y,plot_w,std::max(48,(monitor.h-8)/2)};
    l.waterfall={monitor.x,l.plot.y+l.plot.h+8,plot_w,monitor.h-l.plot.h-8};
    l.events={monitor.x+plot_w+8,monitor.y,monitor.w-plot_w-8,monitor.h};
  }
  if(l.vertical) {
    l.single_node_panel=l.body.h<500;
    const int list_h=l.single_node_panel?l.body.h:(l.body.h-8)/2;
    l.nodes_list={l.body.x,l.body.y,l.body.w,list_h};
    l.node_details=l.single_node_panel?l.body:Rect{l.body.x,l.body.y+list_h+8,l.body.w,l.body.h-list_h-8};
  } else {
    const int list_w=(l.body.w-8)*58/100;
    l.nodes_list={l.body.x,l.body.y,list_w,l.body.h};
    l.node_details={l.body.x+list_w+8,l.body.y,l.body.w-list_w-8,l.body.h};
  }
  l.node_row_count=std::clamp((l.nodes_list.h-84)/44,1,6);
  for(int i=0;i<l.node_row_count;++i)l.node_rows[i]={l.nodes_list.x+8,l.nodes_list.y+32+i*44,l.nodes_list.w-16,44};
  const Rect node_pages={l.nodes_list.x+8,l.nodes_list.y+l.nodes_list.h-52,l.nodes_list.w-16,44};
  l.node_previous=cell(node_pages,0,2,1);l.node_next=cell(node_pages,1,2,1);
  const int traffic_space=l.body.h-60;
  l.traffic_row_count=std::clamp(traffic_space/112,1,6);
  const int row_h=traffic_space/l.traffic_row_count;
  for(int i=0;i<l.traffic_row_count;++i)l.traffic_rows[i]={l.body.x+8,l.body.y+8+i*row_h,l.body.w-16,row_h-4};
  const Rect traffic_pages={l.body.x+8,l.body.y+l.body.h-48,l.body.w-16,44};
  l.traffic_previous=cell(traffic_pages,0,2,1);l.traffic_next=cell(traffic_pages,1,2,1);
  if(l.vertical) {
    const int info_h=std::min(200,l.body.h/3);
    l.map={l.body.x,l.body.y,l.body.w,l.body.h-info_h-8};
    l.map_info={l.body.x,l.map.y+l.map.h+8,l.body.w,info_h};
  } else {
    const int map_w=(l.body.w-8)*7/10;
    l.map={l.body.x,l.body.y,map_w,l.body.h};
    l.map_info={l.body.x+map_w+8,l.body.y,l.body.w-map_w-8,l.body.h};
  }
  l.map_zoom_in={l.map.x+l.map.w-52,l.map.y+8,44,44};
  l.map_zoom_out={l.map_zoom_in.x,l.map_zoom_in.y+52,44,44};
  l.channel_dialog={v.x+16,header.content_top+8,v.w-32,l.tabs.y-header.content_top-24};
  l.channel_close={l.channel_dialog.x+l.channel_dialog.w-52,l.channel_dialog.y+8,44,44};
  l.region_previous={l.channel_dialog.x+12,l.channel_dialog.y+78,44,44};
  l.region_next={l.channel_dialog.x+l.channel_dialog.w-56,l.region_previous.y,44,44};
  l.slot_previous={l.region_previous.x,l.region_previous.y+64,44,44};
  l.slot_next={l.region_next.x,l.slot_previous.y,44,44};
  l.valid=audio_header::header_inside(v,l.body)&&audio_header::header_inside(v,l.tabs);
  for(int i=0;i<l.action_count;++i)l.valid=l.valid&&l.actions[i].w>=44&&l.actions[i].h>=44&&audio_header::header_inside(v,l.actions[i]);
  for(int i=0;i<l.tab_count;++i)l.valid=l.valid&&l.tab[i].w>=44&&l.tab[i].h>=44;
  return l;
}
} // namespace orcsdr::lora::rx_layout
