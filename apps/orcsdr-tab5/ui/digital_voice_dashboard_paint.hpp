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
  // Dry run: when set, every call folds what it would draw into *sig and draws nothing. A page whose signature did not change
  // since the last paint needs no repaint at all.
  uint32_t* sig = nullptr;
  void mix(uint32_t v) {*sig=(*sig^v)*16777619u;}
  void mix_text(const char* s) {
    if (s) for (; *s; ++s) mix(static_cast<uint8_t>(*s));
    mix(0x100u);
  }
  void fillRect(int x,int y,int w,int h,uint16_t c) {
    if (sig) {mix(1);mix(static_cast<uint32_t>(x));mix(static_cast<uint32_t>(y));mix(static_cast<uint32_t>(w));mix(static_cast<uint32_t>(h));mix(c);return;}
    g.fillRect(x,y,w,h,c);
  }
  void fillRoundRect(int x,int y,int w,int h,int r,uint16_t c) {
    if (sig) {mix(2);mix(static_cast<uint32_t>(x));mix(static_cast<uint32_t>(y));mix(static_cast<uint32_t>(w));mix(static_cast<uint32_t>(h));mix(static_cast<uint32_t>(r));mix(c);return;}
    g.fillRoundRect(x,y,w,h,r,c);
  }
  void drawRoundRect(int x,int y,int w,int h,int r,uint16_t c) {
    if (sig) {mix(3);mix(static_cast<uint32_t>(x));mix(static_cast<uint32_t>(y));mix(static_cast<uint32_t>(w));mix(static_cast<uint32_t>(h));mix(static_cast<uint32_t>(r));mix(c);return;}
    g.drawRoundRect(x,y,w,h,r,c);
  }
  void drawFastHLine(int x,int y,int w,uint16_t c) {
    if (sig) {mix(4);mix(static_cast<uint32_t>(x));mix(static_cast<uint32_t>(y));mix(static_cast<uint32_t>(w));mix(c);return;}
    g.drawFastHLine(x,y,w,c);
  }
  void text(const char* s, int x, int y, uint16_t color=kWhite, int size=2,
            Rect clip={20,104,1240,616}) {
    if (!s) return;
    if (sig) {
      mix(5);mix_text(s);mix(static_cast<uint32_t>(x));mix(static_cast<uint32_t>(y));mix(color);mix(static_cast<uint32_t>(size));
      mix(static_cast<uint32_t>(clip.x));mix(static_cast<uint32_t>(clip.y));mix(static_cast<uint32_t>(clip.w));mix(static_cast<uint32_t>(clip.h));
      return;
    }
    g.setClipRect(clip.x,clip.y,clip.w,clip.h);
    g.setFont(nullptr);
    g.setTextSize(size);
    g.setTextDatum(middle_left);
    g.setTextColor(color);
    g.drawString(s,x,y);
    g.clearClipRect();
  }
  void card(Rect r) {
    fillRoundRect(r.x,r.y,r.w,r.h,9,kPanel);
    drawRoundRect(r.x,r.y,r.w,r.h,9,kEdge);
  }
  void label(const char* s,int x,int y,Rect c={20,104,1240,616}) {
    text(s,x,y,kMuted,2,c);
  }
  void title(const char* s,int x,int y,Rect c={20,104,1240,616}) {
    text(s,x,y,kCyan,2,c);
  }
  void hr(int x,int y,int width) {drawFastHLine(x,y,width,kEdge);}
  void button(const char* label,Rect r,bool enabled,bool selected=false) {
    fillRoundRect(r.x,r.y,r.w,r.h,7,selected?kSelected:kPanel);
    drawRoundRect(r.x,r.y,r.w,r.h,7,!enabled?kEdge:selected?kGreen:kCyan);
    text(label,r.x+10,r.y+r.h/2,!enabled?kMuted:selected?kGreen:kWhite,2,r);
  }
  void fact(const char* key,const char* value,int x,int y,Rect clip,uint16_t value_color=kWhite,int value_dx=190) {
    text(key,x,y,kMuted,2,clip);
    text(value,x+value_dx,y,value_color,2,clip);
  }
};
}  // namespace orcsdr::digital_voice_ui::paint
