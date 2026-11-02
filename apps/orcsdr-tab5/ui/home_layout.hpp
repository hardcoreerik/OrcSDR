#pragma once
#include "dashboard_header_layout.hpp"

namespace orcsdr::home {
using LayoutRect=audio_header::HeaderRect;
struct Layout {
  LayoutRect viewport,main,rail,spectrum,waterfall,frequency,contrast,palette,speed,mode;
  LayoutRect span,tune,step,filter,gain,recent,all,footer,popup;
  int recent_row_height=52,recent_row_pitch=60;
  bool portrait=false,legacy=false,valid=false;
};
// Shared geometry for rendering and input. Plot areas flex; controls do not
// inherit an independent X/Y stretch. Legacy coordinates remain the reference.
inline Layout make_layout(LayoutRect v,int content_top) {
  Layout l{};l.viewport=v;l.portrait=v.h>v.w;
  if (v.x==0 && v.y==0 && v.w==1280 && v.h==720) {
    l.legacy=true;l.main={318,80,930,562};l.rail={24,112,280,530};
    l.spectrum={330,112,906,186};l.waterfall={330,301,906,194};
    l.frequency={330,504,350,52};l.contrast={690,516,200,28};
    l.palette={898,504,120,52};l.speed={1026,504,110,52};l.mode={1144,504,92,52};
    l.span={330,564,208,62};l.tune={544,564,208,62};l.step={758,564,208,62};
    l.filter={974,564,142,62};l.gain={1124,564,112,62};
    l.recent={30,158,242,420};l.all={30,590,240,42};l.footer={24,654,1224,48};
    l.popup={340,408,886,300};l.valid=true;return l;
  }
  if (v.w<360 || v.h<400 || content_top<v.y || content_top>=v.y+v.h) return l;
  constexpr int gap=8,button_h=48;
  const int bottom=v.y+v.h;
  l.footer={v.x,bottom-48,v.w,48};
  if (l.portrait) {
    // Full-width plots/readout above stacked control rows and the existing
    // recent-dashboard list. Every secondary control remains reachable.
    const int controls_y=bottom-48-gap-3*(button_h+gap)-200;
    const int plots_bottom=controls_y-gap-2*(button_h+gap);
    const int plots_h=plots_bottom-content_top;
    if (plots_h<160) return l;
    const int spectrum_h=(plots_h-gap)/2;
    l.spectrum={v.x,content_top,v.w,spectrum_h};
    l.waterfall={v.x,content_top+spectrum_h+gap,v.w,plots_h-spectrum_h-gap};
    int y=plots_bottom+gap;
    l.frequency={v.x,y,v.w,button_h};y+=button_h+gap;
    const int third=(v.w-2*gap)/3;
    l.contrast={v.x,y,third,button_h};l.palette={v.x+third+gap,y,third,button_h};
    l.speed={v.x+2*(third+gap),y,v.w-2*(third+gap),button_h};
    y+=button_h+gap;
    const int half=(v.w-gap)/2;
    l.span={v.x,y,half,button_h};l.tune={v.x+half+gap,y,v.w-half-gap,button_h};y+=button_h+gap;
    l.step={v.x,y,half,button_h};l.mode={v.x+half+gap,y,v.w-half-gap,button_h};y+=button_h+gap;
    l.filter={v.x,y,half,button_h};l.gain={v.x+half+gap,y,v.w-half-gap,button_h};y+=button_h+gap;
    l.all={v.x,l.footer.y-gap-button_h,v.w,button_h};
    l.recent={v.x,y,v.w,l.all.y-gap-y};
    l.rail={v.x,y,v.w,l.all.y+button_h-y};
    l.main={v.x,content_top,v.w,y-gap-content_top};
  } else {
    const int rail_w=std::clamp(v.w/5,160,240);
    const int x=v.x+rail_w+gap,w=v.w-rail_w-gap;
    if (w<500) return l;
    l.rail={v.x,content_top,rail_w,l.footer.y-gap-content_top};
    l.all={v.x,l.footer.y-gap-button_h,rail_w,button_h};
    l.recent={v.x,content_top,rail_w,l.all.y-gap-content_top};
    const int controls_y=l.footer.y-gap-button_h;
    const int readout_y=controls_y-gap-button_h;
    const int plot_h=readout_y-gap-content_top;
    if (plot_h<120) return l;
    const int sh=(plot_h-gap)/2;
    l.spectrum={x,content_top,w,sh};l.waterfall={x,content_top+sh+gap,w,plot_h-sh-gap};
    // Readout and secondary plot controls share a row, using physical font
    // metrics when rendered rather than compressing their glyphs.
    const int fw=w*34/100,cw=w*23/100,small=(w-fw-cw-4*gap)/3;
    l.frequency={x,readout_y,fw,button_h};
    l.contrast={x+fw+gap,readout_y,cw,button_h};
    l.palette={l.contrast.x+cw+gap,readout_y,small,button_h};
    l.speed={l.palette.x+small+gap,readout_y,small,button_h};
    l.mode={l.speed.x+small+gap,readout_y,w-(l.speed.x+small+gap-x),button_h};
    // Give the three steppers enough room for normal-size labels and values.
    // Filter/gain need less width because they have no flanking arrow buttons.
    const bool readable_steppers = w >= 3*196 + 2*88 + 4*gap;
    const int chip = readable_steppers ? std::min(112,(w-3*196-4*gap)/2) : (w-4*gap)/5;
    const int unit = readable_steppers ? (w-2*chip-4*gap)/3 : chip;
    l.span={x,controls_y,unit,button_h};l.tune={x+unit+gap,controls_y,unit,button_h};
    l.step={x+2*(unit+gap),controls_y,unit,button_h};
    l.filter={x+3*(unit+gap),controls_y,chip,button_h};
    const int gain_x=l.filter.x+chip+gap;
    l.gain={gain_x,controls_y,x+w-gain_x,button_h};
    l.main={x,content_top,w,l.footer.y-gap-content_top};
  }
  l.popup={v.x,bottom-std::min(360,v.h),v.w,std::min(360,v.h)};
  const LayoutRect required[]={l.main,l.rail,l.spectrum,l.waterfall,l.frequency,l.contrast,
    l.palette,l.speed,l.mode,l.span,l.tune,l.step,l.filter,l.gain,l.recent,l.all,l.footer,l.popup};
  l.valid=true;
  for (auto r:required) if (!audio_header::header_inside(v,r)) l.valid=false;
  const LayoutRect buttons[]={l.frequency,l.contrast,l.palette,l.speed,l.mode,l.span,l.tune,l.step,l.filter,l.gain,l.all};
  for (auto r:buttons) if (r.w<44 || r.h<44) l.valid=false;
  return l;
}
} // namespace orcsdr::home
