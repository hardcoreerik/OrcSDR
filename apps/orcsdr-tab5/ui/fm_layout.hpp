#pragma once

#include "dashboard_header_layout.hpp"

namespace orcsdr::fm {
using Rect = audio_header::HeaderRect;
enum Setting { sound, volume_minus, volume_plus, step, filter_minus, filter_plus,
               graphics, recording, scan, device_settings, home, setting_count };
struct GainGeometry { Rect automatic, slider; };
struct Layout {
  Rect viewport, content, tabs[5], spectrum, waterfall;
  Rect listen_values[6], listen_controls[5], settings[setting_count];
  Rect station[8], health[12], spectrum_info, span_minus, span_value, span_plus;
  GainGeometry gain[3]; // listen, spectrum, settings
  bool legacy = false, valid = false;
};
inline Rect cell(Rect area, int index, int columns, int rows, int gap = 8) {
  const int col = index % columns, row = index / columns;
  const int w = (area.w - (columns - 1) * gap) / columns;
  const int h = (area.h - (rows - 1) * gap) / rows;
  return {area.x + col * (w + gap), area.y + row * (h + gap), w, h};
}
inline Layout make_layout(Rect v, int content_top) {
  Layout l{}; l.viewport = v;
  l.legacy = v.x == 0 && v.y == 0 && v.w == 1280 && v.h == 720;
  if (l.legacy) {
    l.content = {0,132,1280,498};
    for (int i=0;i<5;++i) l.tabs[i]={i*256,630,256,90};
    l.spectrum={46,246,1188,145};l.waterfall={46,420,1188,130};
    const int widths[]={220,220,280,220,220};int x=24;
    for(int i=0;i<5;++i){l.listen_controls[i]={x,485,widths[i],120};x+=widths[i]+10;}
    l.listen_values[0]={24,160,300,140};
    l.gain[0]={{44,405,80,42},{145,396,145,66}};
    l.gain[1]={{928,561,82,44},{1024,560,190,66}};
    l.gain[2]={{48,542,190,58},{280,544,900,66}};
    l.span_minus={390,565,70,42};l.span_plus={820,565,70,42};
    l.settings[sound]={48,215,250,64};l.settings[volume_minus]={320,215,132,64};
    l.settings[volume_plus]={468,215,132,64};l.settings[step]={48,300,250,64};
    l.settings[filter_minus]={320,300,132,64};l.settings[filter_plus]={468,300,132,64};
    l.settings[graphics]={48,385,552,64};l.settings[recording]={48,470,552,64};
    l.settings[scan]={664,215,568,64};l.settings[device_settings]={664,300,568,64};
    l.settings[home]={664,385,568,64};l.valid=true;return l;
  }
  const int tab_y=v.y+v.h-48;
  if(v.w<360 || content_top<v.y || tab_y-content_top<300) return l;
  l.content={v.x,content_top,v.w,tab_y-content_top};
  for(int i=0;i<5;++i) {
    const int left=v.x+i*v.w/5,right=v.x+(i+1)*v.w/5;
    l.tabs[i]={left,tab_y,right-left,48};
  }
  Rect inner={v.x+8,content_top+8,v.w-16,l.content.h-16};
  const bool wide=v.w>=800;
  // Controls reflow by available width; text and icons retain their proportions.
  const int listen_cols=v.w>=560?5:3, listen_rows=listen_cols==5?1:2;
  const int controls_h=listen_rows*48+(listen_rows-1)*8;
  Rect controls={inner.x,inner.y+inner.h-controls_h,inner.w,controls_h};
  for(int i=0;i<5;++i)l.listen_controls[i]=cell(controls,i,listen_cols,listen_rows);
  auto gains=[&](int index,int bottom) {
    l.gain[index]={{inner.x,bottom-56,104,56},{inner.x+112,bottom-56,inner.w-112,56}};
  };
  gains(0,controls.y-8);gains(2,inner.y+inner.h);gains(1,inner.y+inner.h);
  Rect listen={inner.x,inner.y,inner.w,l.gain[0].automatic.y-inner.y-8};
  const int summary_h=std::min(124,listen.h/2);
  for(int i=0;i<3;++i) l.listen_values[i]={listen.x,listen.y+i*summary_h/3,listen.w,summary_h/3};
  Rect metrics={listen.x,listen.y+summary_h+4,listen.w,listen.h-summary_h-4};
  for(int i=0;i<3;++i)l.listen_values[i+3]=cell(metrics,i,3,1);
  Rect setting_area={inner.x,inner.y,inner.w,l.gain[2].automatic.y-inner.y-8};
  const int setting_cols=wide?3:2,setting_rows=wide?4:6;
  for(int i=0;i<setting_count;++i)l.settings[i]=cell(setting_area,i,setting_cols,setting_rows);
  for(int i=0;i<8;++i)l.station[i]=cell(inner,i,wide?4:2,wide?2:4);
  for(int i=0;i<12;++i)l.health[i]=cell(inner,i,wide?3:2,wide?4:6);
  l.spectrum_info={inner.x,inner.y,inner.w,44};
  const int span_y=l.gain[1].automatic.y-52;
  l.span_minus={inner.x,span_y,48,44};l.span_plus={inner.x+inner.w-48,span_y,48,44};
  l.span_value={inner.x+56,span_y,inner.w-112,44};
  const int plot_y=inner.y+52,available=span_y-plot_y-8;
  const int plot_w=std::min(inner.w,1188),plot_x=inner.x+(inner.w-plot_w)/2;
  // Existing sprite budget: at most 1188 * 145 * 2 bytes, without a framebuffer.
  const int sh=std::min(available*2/3,(1188*145)/plot_w);
  const int wh=std::min(130,available-sh-8);
  l.spectrum={plot_x,plot_y,plot_w,sh};l.waterfall={plot_x,plot_y+sh+8,plot_w,wh};
  l.valid=sh>=44 && wh>=16;
  for(auto r:l.tabs)l.valid=l.valid && audio_header::header_inside(v,r);
  for(auto r:l.settings)l.valid=l.valid && r.w>=44 && r.h>=44 && audio_header::header_inside(l.content,r);
  for(auto r:l.listen_controls)l.valid=l.valid && r.w>=44 && r.h>=44 && audio_header::header_inside(l.content,r);
  for(auto g:l.gain)l.valid=l.valid && g.slider.w>=44 && audio_header::header_inside(l.content,g.slider);
  return l;
}
} // namespace orcsdr::fm
