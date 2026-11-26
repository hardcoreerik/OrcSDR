#pragma once
// Drawing primitives and palette shared by the Digital Voice dashboard translation units. Draw-only: no state, no receiver access.
#include "digital_voice_dashboard_layout.hpp"
#include <M5Unified.h>
namespace orcsdr::digital_voice_ui::paint {
using layout::Rect;
// Existing OrcSDR RGB565 palette, taken from main's AIS/Dead Air source.
constexpr uint16_t kBg=TFT_BLACK, kPanel=0x0841, kEdge=0x2945, kCyan=0x2e7f,
                   kGreen=0x6fe8, kMuted=0x8c71, kAmber=0xfd20, kRed=0xf800,
                   kWhite=TFT_WHITE, kSelected=0x1264;
struct Paint {
  lgfx::LovyanGFX& g;
  void text(const char* s, int x, int y, uint16_t color=kWhite, int size=2,
            Rect clip={20,104,1240,616}) {
    if (!s) return;
    g.setClipRect(clip.x,clip.y,clip.w,clip.h);
    g.setFont(nullptr);
    g.setTextSize(size);
    g.setTextDatum(middle_left);
    g.setTextColor(color);
    g.drawString(s,x,y);
    g.clearClipRect();
  }
  void card(Rect r) {
    g.fillRoundRect(r.x,r.y,r.w,r.h,9,kPanel);
    g.drawRoundRect(r.x,r.y,r.w,r.h,9,kEdge);
  }
  void label(const char* s,int x,int y,Rect c={20,104,1240,616}) {
    text(s,x,y,kMuted,2,c);
  }
  void title(const char* s,int x,int y,Rect c={20,104,1240,616}) {
    text(s,x,y,kCyan,2,c);
  }
  void hr(int x,int y,int width) {g.drawFastHLine(x,y,width,kEdge);}
  void button(const char* label,Rect r,bool enabled,bool selected=false) {
    g.fillRoundRect(r.x,r.y,r.w,r.h,7,selected?kSelected:kPanel);
    g.drawRoundRect(r.x,r.y,r.w,r.h,7,!enabled?kEdge:selected?kGreen:kCyan);
    text(label,r.x+10,r.y+r.h/2,!enabled?kMuted:selected?kGreen:kWhite,2,r);
  }
  void fact(const char* key,const char* value,int x,int y,Rect clip,uint16_t value_color=kWhite,int value_dx=190) {
    text(key,x,y,kMuted,2,clip);
    text(value,x+value_dx,y,value_color,2,clip);
  }
};
}  // namespace orcsdr::digital_voice_ui::paint
