#include "orcui/layout.hpp"
#include <algorithm>
#include <cmath>
namespace orcsdr::orcui {
namespace {
int clampi(int v,int lo,int hi){return std::clamp(v,lo,hi);}
Rect inset(Rect r,int pad){return {r.x+pad,r.y+pad,std::max(0,r.w-2*pad),std::max(0,r.h-2*pad)};}
}
const char* form_name(Form form){
  switch(form){
    case Form::compact:return "compact";
    case Form::square:return "square";
    case Form::wide:return "wide";
    case Form::ultrawide:return "ultrawide";
    case Form::round:return "round";
  }
  return "unknown";
}
Layout plan(const Display& display){
  Layout l{};
  if(display.width<240 || display.height<240)return l;
  const auto& in=display.insets;
  if(in.left<0 || in.right<0 || in.top<0 || in.bottom<0)return l;
  l.safe={in.left,in.top,display.width-in.left-in.right,display.height-in.top-in.bottom};
  if(l.safe.w<220 || l.safe.h<220)return l;
  if(display.shape==Shape::circle){
    const int side=static_cast<int>(std::floor(std::min(l.safe.w,l.safe.h)*0.70710678));
    l.safe={l.safe.x+(l.safe.w-side)/2,l.safe.y+(l.safe.h-side)/2,side,side};
    l.form=Form::round;
  }else if(l.safe.w>=1920 && l.safe.w>=2*l.safe.h){
    l.form=Form::ultrawide;
  }else if(l.safe.w>=1024 && l.safe.w*10>=l.safe.h*14){
    l.form=Form::wide;
  }else if(l.safe.w>=560 && l.safe.h>=560 && l.safe.w*10>=l.safe.h*8){
    l.form=Form::square;
  }else l.form=Form::compact;
  const bool round=l.form==Form::round;
  const int gap=round?5:clampi(std::min(l.safe.w,l.safe.h)/65,7,14);
  const Rect body=inset(l.safe,round?3:gap);
  const int header_h=round?42:clampi(body.h/10,48,74);
  const int status_h=round?0:clampi(body.h/16,30,42);
  const int readout_h=round?66:clampi(body.h/9,56,84);
  const int controls_h=round?0:(l.form==Form::square?100:l.form==Form::compact?88:76);
  l.header={body.x,body.y,body.w,header_h};
  l.menu_toggle={l.header.x+2,l.header.y+2,round?40:52,header_h-4};
  if(status_h)l.status={body.x,body.bottom()-status_h,body.w,status_h};
  const int content_top=l.header.bottom()+gap;
  const int content_bottom=l.status.valid()?l.status.y-gap:body.bottom();
  const bool sidebar=l.form==Form::wide || l.form==Form::ultrawide;
  const bool inspector=l.form==Form::ultrawide;
  l.show_sidebar=sidebar;
  l.show_inspector=inspector;
  l.show_controls=!round;
  int left=body.x,right=body.right();
  if(sidebar){
    const int width=clampi(body.w/5,194,260);
    l.sidebar={left,content_top,width,content_bottom-content_top};
    left=l.sidebar.right()+gap;
  }
  if(inspector){
    const int width=clampi(body.w/5,250,420);
    l.inspector={right-width,content_top,width,content_bottom-content_top};
    right=l.inspector.x-gap;
  }
  const int available_h=content_bottom-content_top;
  const int graph_h=available_h-readout_h-controls_h-(round?gap*2:gap*3);
  if(right-left<150 || graph_h<60)return l;
  const int spectrum_h=graph_h*(round?55:45)/100;
  l.spectrum={left,content_top,right-left,spectrum_h};
  l.waterfall={left,l.spectrum.bottom()+gap,right-left,graph_h-spectrum_h};
  l.frequency={left,l.waterfall.bottom()+gap,right-left,readout_h};
  if(l.show_controls){
    l.controls={left,l.frequency.bottom()+gap,right-left,controls_h};
    const int rows=l.form==Form::square || l.form==Form::compact?2:1;
    const int cols=rows==1?5:3;
    const int cell_h=(controls_h-(rows-1)*gap)/rows;
    for(int i=0;i<5;++i){
      const int row=i/cols,col=i%cols;
      const int count=row==0?cols:2;
      const int cell_w=(l.controls.w-(count-1)*gap)/count;
      l.control_buttons[i]={left+col*(cell_w+gap),
        l.controls.y+row*(cell_h+gap),cell_w,cell_h};
    }
  }
  const int menu_w=std::min(body.w,clampi(body.w*7/10,180,360));
  l.menu_overlay={body.x,content_top,menu_w,std::max(0,content_bottom-content_top)};
  l.valid=l.spectrum.valid() && l.waterfall.valid() && l.frequency.valid() &&
    (!l.show_controls || l.controls.bottom()<=content_bottom) &&
    l.frequency.bottom()<=content_bottom;
  return l;
}
}