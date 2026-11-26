#pragma once
// Drawing primitives and palette shared by the Digital Voice dashboard translation units. Draw-only: no receiver access.
#include "digital_voice_dashboard_layout.hpp"
#include <M5Unified.h>
namespace orcsdr::digital_voice_ui::paint {
using layout::Rect;
// Existing OrcSDR RGB565 palette, taken from main's AIS/Dead Air source.
constexpr uint16_t kBg=TFT_BLACK, kPanel=0x0841, kEdge=0x2945, kCyan=0x2e7f,
                   kGreen=0x6fe8, kMuted=0x8c71, kAmber=0xfd20, kRed=0xf800,
                   kWhite=TFT_WHITE, kSelected=0x1264;

// What a page drew the last time, one entry per drawing call in order (a hash and, for text, the pixel width). A repaint of the same
// page draws only the calls whose hash changed: an unchanged card, label or value is not touched, and a changed text clears just its
// own rectangle (old or new width, whichever is wider, in the colour of the fill under it) before the new text is drawn. Nothing is
// ever blanked and redrawn as a whole, which is what made every page flash whenever one value moved. The arrays live in PSRAM
// (allocated by the dashboard); without them Paint draws everything as before.
constexpr unsigned kSlotOps=192, kSlots=18;     // each widget (a page, a list row, a panel) has its own run of calls
struct Diff {
  uint16_t used[kSlots]={};   // how many calls the widget made last time: a different count means its structure changed
  uint32_t* h=nullptr;     // 0 = nothing drawn yet at this index
  uint16_t* w=nullptr;     // text width of the last text drawn at this index
  int16_t* xs=nullptr;     // where that text was drawn (x, y)
  int16_t* ys=nullptr;
  uint16_t cap=0;
  void reset() {
    for(unsigned i=0;i<kSlots;++i)used[i]=0;
    if(h)for(uint16_t i=0;i<cap;++i){h[i]=0;w[i]=0;xs[i]=0;ys[i]=0;}
  }
  void reset_slot(unsigned id) {
    used[id]=0;
    if(h)for(unsigned i=id*kSlotOps;i<(id+1)*kSlotOps&&i<cap;++i){h[i]=0;w[i]=0;xs[i]=0;ys[i]=0;}
  }
};

struct Paint {
  lgfx::LovyanGFX& g;
  // Dry run: when set, every call folds what it would draw into *sig and draws nothing. A page whose signature did not change
  // since the last paint needs no repaint at all.
  uint32_t* sig = nullptr;
  // Diff painting (see Diff). `base` selects the slot of the buffer this widget uses, `limit` is one past its last index.
  Diff* diff = nullptr;
  uint16_t base = 0, limit = 0, di = 0, slot_id = 0;
  uint16_t base_bg = kBg;      // colour under text when no fill of this widget covers it (the card the widget sits on)
  bool broken_ = false;
  static constexpr int kFills = 24, kDirty = 8;
  struct Fill {int x,y,w,h;uint16_t c;};
  Fill fills_[kFills]{};
  int nfills_ = 0;
  Rect dirty_[kDirty]{};
  int ndirty_ = 0;
  bool all_dirty_ = false;

  void mix(uint32_t v) {*sig=(*sig^v)*16777619u;}
  void mix_text(const char* s) {
    if (s) for (; *s; ++s) mix(static_cast<uint8_t>(*s));
    mix(0x100u);
  }
  // ---- diff helpers ----
  static uint32_t hv(uint32_t h,uint32_t v) {return (h^v)*16777619u;}
  uint32_t hash_geom(uint32_t kind,int x,int y,int w,int h,int r,uint16_t c) const {
    uint32_t k=2166136261u;
    k=hv(k,kind);k=hv(k,static_cast<uint32_t>(x));k=hv(k,static_cast<uint32_t>(y));k=hv(k,static_cast<uint32_t>(w));
    k=hv(k,static_cast<uint32_t>(h));k=hv(k,static_cast<uint32_t>(r));k=hv(k,c);
    return k|1u;
  }
  // The call's index in the diff buffer, or -1 when there is no diff buffer for it.
  int slot() {
    if (!diff||!diff->h) return -1;
    const uint32_t i=static_cast<uint32_t>(base)+di++;
    return (i<limit&&i<diff->cap)?static_cast<int>(i):-1;
  }
  bool in_dirty(int x,int y) const {
    if (all_dirty_) return true;
    for (int i=0;i<ndirty_;++i) if (dirty_[i].contains(x,y)) return true;
    return false;
  }
  void note_fill(int x,int y,int w,int h,uint16_t c) {
    if (w<40||h<20) return;                 // a thin bar or a bar's background is never what text sits on; remembering it would crowd out the cards and buttons
    if (nfills_<kFills) fills_[nfills_++]={x,y,w,h,c};
  }
  void mark_dirty(int x,int y,int w,int h) {
    if (ndirty_<kDirty) dirty_[ndirty_++]={x,y,w,h};
    else all_dirty_=true;
  }
  // Start a widget: its calls are compared with what the same widget drew last time (slot `id`).
  void use(unsigned id,uint16_t bg=kBg) {
    slot_id=static_cast<uint16_t>(id);base=static_cast<uint16_t>(id*kSlotOps);limit=static_cast<uint16_t>(base+kSlotOps);di=0;
    base_bg=bg;nfills_=0;ndirty_=0;all_dirty_=false;broken_=false;
  }
  // End a widget. True when its structure changed (a different number of calls), in which case its remembered calls were dropped and
  // the caller draws it once more in full.
  bool done() {
    if (!diff||!diff->h) return false;
    const bool changed=broken_||(diff->used[slot_id]!=0&&diff->used[slot_id]!=di);
    if (changed) {diff->reset_slot(slot_id);return true;}
    diff->used[slot_id]=di;
    return false;
  }
  // A widget inside another (the voice panel inside the LIVE page): its calls have their own slot, so a change in how many calls it makes
  // never shifts the outer widget's calls. save()/restore() keep the outer widget's position.
  struct Saved {uint16_t id,base,limit,di,bg;int nf,nd;bool ad;Fill fills[kFills];Rect dirty[kDirty];};
  void save(Saved& o) const {
    o.id=slot_id;o.base=base;o.limit=limit;o.di=di;o.bg=base_bg;o.nf=nfills_;o.nd=ndirty_;o.ad=all_dirty_;
    for(int i=0;i<nfills_;++i)o.fills[i]=fills_[i];
    for(int i=0;i<ndirty_;++i)o.dirty[i]=dirty_[i];
  }
  void restore(const Saved& o) {
    slot_id=o.id;base=o.base;limit=o.limit;di=o.di;base_bg=o.bg;nfills_=o.nf;ndirty_=o.nd;all_dirty_=o.ad;
    for(int i=0;i<nfills_;++i)fills_[i]=o.fills[i];
    for(int i=0;i<ndirty_;++i)dirty_[i]=o.dirty[i];
  }
  uint16_t bg_at(int x,int y) const {
    for (int i=nfills_-1;i>=0;--i) {
      const Fill& f=fills_[i];
      if (x>=f.x&&x<f.x+f.w&&y>=f.y&&y<f.y+f.h) return f.c;
    }
    return base_bg;
  }
  // Shared by the shape calls: returns true when the call must be drawn.
  bool shape_needed(uint32_t kind,int x,int y,int w,int h,int r,uint16_t c,bool is_fill) {
    const int i=slot();
    if (i<0) {if(is_fill)note_fill(x,y,w,h,c);return true;}
    const uint32_t k=hash_geom(kind,x,y,w,h,r,c);
    const bool known=diff->h[i]!=0;
    const bool same=known&&diff->h[i]==k;
    diff->h[i]=k;
    if (is_fill) note_fill(x,y,w,h,c);
    if (same) return false;
    if (known) mark_dirty(x,y,w,h);       // a shape that changed may paint over text that did not: redraw what lies inside it
    return true;
  }

  void fillRect(int x,int y,int w,int h,uint16_t c) {
    if (sig) {mix(1);mix(static_cast<uint32_t>(x));mix(static_cast<uint32_t>(y));mix(static_cast<uint32_t>(w));mix(static_cast<uint32_t>(h));mix(c);return;}
    if (!shape_needed(1,x,y,w,h,0,c,true)) return;
    g.fillRect(x,y,w,h,c);
  }
  void fillRoundRect(int x,int y,int w,int h,int r,uint16_t c) {
    if (sig) {mix(2);mix(static_cast<uint32_t>(x));mix(static_cast<uint32_t>(y));mix(static_cast<uint32_t>(w));mix(static_cast<uint32_t>(h));mix(static_cast<uint32_t>(r));mix(c);return;}
    if (!shape_needed(2,x,y,w,h,r,c,true)) return;
    g.fillRoundRect(x,y,w,h,r,c);
  }
  void drawRoundRect(int x,int y,int w,int h,int r,uint16_t c) {
    if (sig) {mix(3);mix(static_cast<uint32_t>(x));mix(static_cast<uint32_t>(y));mix(static_cast<uint32_t>(w));mix(static_cast<uint32_t>(h));mix(static_cast<uint32_t>(r));mix(c);return;}
    if (!shape_needed(3,x,y,w,h,r,c,false)) return;
    g.drawRoundRect(x,y,w,h,r,c);
  }
  void drawFastHLine(int x,int y,int w,uint16_t c) {
    if (sig) {mix(4);mix(static_cast<uint32_t>(x));mix(static_cast<uint32_t>(y));mix(static_cast<uint32_t>(w));mix(c);return;}
    if (!shape_needed(4,x,y,w,1,0,c,false)) return;
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
    const int i=slot();
    g.setFont(nullptr);
    g.setTextSize(size);
    if (i>=0) {
      uint32_t k=hash_geom(5,x,y,clip.x,clip.y,size,color);
      k=hv(k,static_cast<uint32_t>(clip.w));k=hv(k,static_cast<uint32_t>(clip.h));
      for (const char* p=s;*p;++p) k=hv(k,static_cast<uint8_t>(*p));
      k|=1u;
      const bool known=diff->h[i]!=0;
      if (known&&diff->h[i]==k&&!in_dirty(x,y)) return;               // the same text in the same place: leave it alone
      const int nw=g.textWidth(s);
      if (known) {                                                      // clear exactly the old and new text, nothing else
        const int th=g.fontHeight();
        g.setClipRect(clip.x,clip.y,clip.w,clip.h);
        const int ox=diff->xs[i],oy=diff->ys[i];
        if (diff->w[i]) g.fillRect(ox,oy-th/2,diff->w[i],th,bg_at(ox,oy));
        g.fillRect(x,y-th/2,nw,th,bg_at(x,y));
      }
      diff->h[i]=k;diff->w[i]=static_cast<uint16_t>(nw);diff->xs[i]=static_cast<int16_t>(x);diff->ys[i]=static_cast<int16_t>(y);
    }
    g.setClipRect(clip.x,clip.y,clip.w,clip.h);
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
