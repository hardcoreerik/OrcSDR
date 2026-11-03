#pragma once
#include "dashboard_header_layout.hpp"
#include <algorithm>
#include <cstring>
#include <cstdio>

// Per-dashboard coordinate adapter for the imported TX Lab drawing primitives.
// Fonts keep their native size; this does not change ScaledDisplay globally.
namespace orcsdr::lora::tx_native {
using Rect=audio_header::HeaderRect;
struct Layout {
  Rect viewport,status,menu,body,tabs,tab[6];
  bool vertical=false,valid=false;
  int x(int source) const {
    const int origin=vertical?16:24,span=vertical?536:1224;
    return body.x+(source-origin)*body.w/span;
  }
  int y(int source) const {
    if(vertical) {
      const int action_top=body.y+body.h-60;
      if(source<=1002)return body.y+(source-225)*(action_top-8-body.y)/777;
      if(source<1026)return action_top-8+(source-1002)*8/24;
      if(source<=1098)return action_top+(source-1026)*44/72;
      return action_top+44+(source-1098)*16/27;
    }
    const int action_top=body.y+body.h-52;
    if(source<=522) return body.y+(source-138)*(action_top-8-body.y)/384;
    if(source<540) return action_top-8+(source-522)*8/18;
    if(source<=602) return action_top+(source-540)*44/62;
    return action_top+44+(source-602)*8/16;
  }
  Rect bounds(int sx,int sy,int sw,int sh) const {return {x(sx),y(sy),x(sx+sw)-x(sx),y(sy+sh)-y(sy)};}
  Rect mode(int i) const {
    const int left=menu.x+i*menu.w/3,right=menu.x+(i+1)*menu.w/3;
    return {left,menu.y,right-left,menu.h};
  }
  Rect action(bool send) const {return vertical?bounds(send?290:28,1026,244,72):bounds(send?356:52,540,286,62);}
};
inline Layout make(Rect v) {
  Layout l{};l.viewport=v;l.vertical=v.h>v.w;
  const auto h=audio_header::make_header_layout(v);if(!h.valid)return l;
  l.status=h.status;
  l.tabs={v.x,v.y+v.h-(l.vertical?96:48),v.w,l.vertical?96:48};
  l.menu={v.x,h.content_top,v.w,44};
  l.body={v.x,l.menu.y+52,v.w,l.tabs.y-l.menu.y-60};
  for(int i=0;i<6;++i) {
    const int count=l.vertical?3:6,row=l.vertical?i/3:0,col=i%count;
    const int left=v.x+col*v.w/count,right=v.x+(col+1)*v.w/count;
    l.tab[i]={left,l.tabs.y+row*48,right-left,48};
  }
  l.valid=l.body.h>200 && audio_header::header_inside(v,l.body) && audio_header::header_inside(v,l.menu) && audio_header::header_inside(v,l.tabs);
  for(bool send:{false,true}) {const auto r=l.action(send);l.valid=l.valid && r.w>=44 && r.h>=44 && audio_header::header_inside(l.body,r);}
  return l;
}
template<class G> class Canvas {
 public:
  Canvas(G& display,const Layout& layout,bool menu=false):g(display),l(layout),menu_(menu) {}
  int x(int a)const {return menu_?l.menu.x+(a-(l.vertical?16:680))*l.menu.w/(l.vertical?534:540):l.x(a);}
  int y(int a)const {return menu_?l.menu.y+(a-(l.vertical?155:174))*l.menu.h/(l.vertical?56:48):l.y(a);}
  void setTextDatum(textdatum_t d){g.setTextDatum(d);}
  void setTextColor(uint16_t c){g.setTextColor(c);}
  void setTextSize(int s){g.setTextSize(std::max(2,s));}
  void drawString(const char* s,int a,int b){
    const int available=l.body.x+l.body.w-8-x(a);
    // A long help line may wrap, but never shrink below the readable minimum.
    if(!menu_ && g.textWidth(s)>available) {
      char line[128]{};size_t offset=0;int row=0;
      while(s[offset]) {
        size_t n=std::min(std::strlen(s+offset),sizeof(line)-1);
        do {std::memcpy(line,s+offset,n);line[n]=0;}while(n>1&&g.textWidth(line)>available&&--n);
        if(s[offset+n]) {
          size_t split=n;while(split>0&&s[offset+split]!=' ')--split;
          if(split>0)n=split;
        }
        std::memcpy(line,s+offset,n);line[n]=0;
        g.drawString(line,x(a),y(b)+row*20);++row;offset+=n;
        while(s[offset]==' ')++offset;
      }
    } else g.drawString(s,x(a),y(b));
  }
  void drawField(int a,int b,int w,const char* name,const char* value) {
    g.setTextSize(2);g.setTextDatum(middle_left);
    if(l.vertical) {
      g.setTextColor(0x2e7f);g.drawString(name,x(a),y(b+10));
      g.setTextColor(0xffff);g.drawString(value,x(a),y(b+36));
    } else {
      char line[128]{};std::snprintf(line,sizeof(line),"%s: %s",name,value);
      g.setTextColor(0xffff);g.drawString(line,x(a),y(b+28));
    }
    g.drawFastHLine(x(a),y(b+55),x(a+w)-x(a),0x2945);
  }
  void fillRect(int a,int b,int w,int h,uint16_t c){
    const int height=std::min(l.body.y+l.body.h-2-y(b),std::max(20,y(b+h)-y(b)));
    g.fillRect(x(a),y(b),x(a+w)-x(a),height,c);
  }
  void fillRoundRect(int a,int b,int w,int h,int r,uint16_t c){g.fillRoundRect(x(a),y(b),x(a+w)-x(a),y(b+h)-y(b),r,c);}
  void drawRoundRect(int a,int b,int w,int h,int r,uint16_t c){g.drawRoundRect(x(a),y(b),x(a+w)-x(a),y(b+h)-y(b),r,c);}
  void drawFastHLine(int a,int b,int w,uint16_t c){g.drawFastHLine(x(a),y(b),x(a+w)-x(a),c);}
  void drawFastVLine(int a,int b,int h,uint16_t c){g.drawFastVLine(x(a),y(b),y(b+h)-y(b),c);}
 private:G& g;const Layout& l;bool menu_;
};
// ADL customization keeps the same imported page templates. Constrained
// landscape fields use one readable row instead of two compressed rows.
template<class G> void field(Canvas<G>& gfx,int x,int y,int w,const char* name,const char* value) {
  gfx.drawField(x,y,w,name,value);
}
}
